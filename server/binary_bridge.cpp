#include "binary_bridge.hpp"
#include "botched/serialize.hpp"
#include <algorithm>
#include <random>
#include <limits>

namespace botched::binary {
namespace {
Bytes fault(unsigned ex, unsigned code) {
    return {(std::uint8_t)255, (std::uint8_t)ex, (std::uint8_t)(ex >> 8), (std::uint8_t)code};
}
void random_bytes(std::array<std::uint8_t, 16> &b) {
    std::random_device rd;
    for (auto &x : b)
        x = (std::uint8_t)rd();
}
bool secret_equal(const std::array<std::uint8_t, 16> &a, const std::uint8_t *b) {
    unsigned d = 0;
    for (size_t i = 0; i < a.size(); ++i)
        d |= a[i] ^ b[i];
    return d == 0;
}
std::string string(const Json &j, const char *k) { return j.get_string(k).value_or(""); }
std::uint32_t integer(const Json &j, const char *k) { return (std::uint32_t)j.get_int(k).value_or(0); }
} // namespace
struct Bridge::Session {
    Connection connection = 0;
    bool connected = false;
    std::array<std::uint8_t, 16> secret{};
    unsigned payload = 240, profile = 3, last_exchange = 0;
    std::int64_t touched = 0;
    Binding binding;
    // Fixed cache capacity reserved at session creation, never after a game mutation.
    std::array<std::uint8_t, 240> request{}, reply{};
    size_t request_size = 0, reply_size = 0;
    Bytes transfer;
    std::uint32_t transfer_id = 0, high_water = 0;
    unsigned origin = 0;
    bool error = false;
    std::uint32_t furthest_end = 0;
};
Bridge::Bridge(server::GameServer &s, const server::Clock &c) : Bridge(s, c, Limits{}) {}
Bridge::Bridge(server::GameServer &s, const server::Clock &c, Limits l) : server_(s), clock_(c), limits_(l) {
    limits_.sessions = std::min<size_t>(64, l.sessions);
    limits_.transfer_bytes = std::min<size_t>(BW_MAX_TRANSFER, l.transfer_bytes);
    limits_.global_bytes = std::min<size_t>(64u << 20, l.global_bytes);
    random_bytes(epoch_);
}
Bridge::~Bridge() = default;
void Bridge::expire() {
    auto now = clock_.now_unix_ms();
    std::erase_if(sessions_, [&](const auto &s) { return now - s->touched >= 1800000; });
}
void Bridge::disconnect(Connection c) {
    for (auto &s : sessions_)
        if (s->connected && s->connection == c)
            s->connected = false;
}
size_t Bridge::session_count() const { return sessions_.size(); }
size_t Bridge::transfer_storage() const {
    size_t n = 0;
    for (const auto &s : sessions_)
        n += s->transfer.capacity();
    return n;
}
std::uint32_t Bridge::map_handle(const std::string &id) {
    auto i = map_handles_.find(id);
    if (i != map_handles_.end())
        return i->second;
    if (!next_map_)
        throw CodecError("map handle exhausted");
    auto h = next_map_++;
    map_handles_.emplace(id, h);
    return h;
}
Bytes Bridge::hello(Connection c, std::span<const std::uint8_t> p) {
    if (!bw_validate_packet(p.data(), p.size(), 240))
        return fault(0, 2);
    Reader r(p.subspan(3));
    unsigned min = r.b(), max = r.b();
    r.b();
    unsigned capacity = r.h(), profile = r.b();
    auto features = r.i();
    auto resume = r.b();
    auto secret = bw_read_bytes(&r.r, 16);
    if (min > 1 || max < 1 || features != 63)
        return fault(0, 1);
    Session *chosen = nullptr;
    if (resume) {
        for (auto &s : sessions_)
            if (secret_equal(s->secret, secret))
                chosen = s.get();
        if (!chosen)
            return fault(0, 4);
        if (chosen->profile != profile || capacity < chosen->payload)
            return fault(0, 1);
    } else {
        if (sessions_.size() >= limits_.sessions)
            return fault(0, 9);
        auto s = std::make_unique<Session>();
        do {
            random_bytes(s->secret);
        } while (std::any_of(sessions_.begin(), sessions_.end(),
                             [&](const auto &old) { return secret_equal(old->secret, s->secret.data()); }));
        s->payload = capacity;
        s->profile = profile;
        chosen = s.get();
        sessions_.push_back(std::move(s));
    }
    for (auto &s : sessions_)
        if (s.get() != chosen && s->connected && s->connection == c)
            s->connected = false;
    chosen->connection = c;
    chosen->connected = true;
    chosen->touched = clock_.now_unix_ms();
    Fields f;
    f.b(1).b(0).h(chosen->payload).b(chosen->profile).i(63);
    bw_write_bytes(&f.w, epoch_.data(), 16);
    bw_write_bytes(&f.w, chosen->secret.data(), 16);
    f.h(chosen->last_exchange).h(1800);
    return packet(0x81, 0, f.span());
}
Bytes Bridge::handle(Connection c, std::span<const std::uint8_t> p) {
    expire();
    unsigned ex = p.size() >= 3 ? p[1] | (unsigned)p[2] << 8 : 0;
    if (p.empty())
        return fault(ex, 2);
    if (p[0] == 1)
        return hello(c, p);
    Session *s = nullptr;
    for (auto &entry : sessions_)
        if (entry->connected && entry->connection == c)
            s = entry.get();
    if (!s)
        return fault(ex, 3);
    if (p.size() < 3 || p.size() > s->payload)
        return fault(ex, 2);
    if (ex == s->last_exchange && ex) {
        if (p.size() != s->request_size || !std::equal(p.begin(), p.end(), s->request.begin()))
            return fault(ex, 5);
        s->touched = clock_.now_unix_ms();
        return Bytes(s->reply.begin(), s->reply.begin() + s->reply_size);
    }
    if (!ex || s->last_exchange == 65535 || ex != s->last_exchange + 1)
        return fault(ex, 6);
    Bytes result;
    if (!bw_validate_packet(p.data(), p.size(), s->payload) || p[0] >= 0x80)
        result = fault(ex, 2);
    else {
        try {
            result = dispatch(*s, p);
            s->touched = clock_.now_unix_ms();
        } catch (const CodecError &) {
            result = fault(ex, 9);
        } catch (const std::bad_alloc &) {
            result = fault(ex, 9);
        }
    }
    s->last_exchange = ex;
    s->request_size = p.size();
    std::copy(p.begin(), p.end(), s->request.begin());
    s->reply_size = result.size();
    std::copy(result.begin(), result.end(), s->reply.begin());
    return result;
}
Bytes Bridge::page(const Session &s, unsigned ex, std::uint32_t offset) const {
    size_t end = offset;
    while (end < s.transfer.size()) {
        auto next = end + s.transfer[end + 1] + 2;
        if (next - offset > s.payload - 22)
            break;
        end = next;
    }
    Bytes p(22 + end - offset);
    bw_writer w;
    bw_writer_init(&w, p.data(), p.size());
    bw_write_u8(&w, 0x82);
    bw_write_u16(&w, ex);
    bw_write_u32(&w, s.transfer_id);
    bw_write_u32(&w, offset);
    bw_write_u32(&w, s.transfer.size());
    bw_write_u32(&w, s.high_water);
    bw_write_u8(&w, s.origin);
    bw_write_u8(&w, (s.error ? 2 : 0) | (end == s.transfer.size() ? 1 : 0));
    bw_write_u8(&w, end - offset);
    bw_write_bytes(&w, s.transfer.data() + offset, end - offset);
    return p;
}
Bytes Bridge::dispatch(Session &s, std::span<const std::uint8_t> p) {
    unsigned op = p[0], ex = p[1] | (unsigned)p[2] << 8;
    Reader reader(p.subspan(3));
    if (op == 3) {
        auto tid = reader.i(), offset = reader.i();
        if (tid != s.transfer_id || s.transfer.empty())
            return fault(ex, 8);
        if (offset >= s.transfer.size() || offset > s.furthest_end)
            return fault(ex, 7);
        size_t cursor = 0;
        while (cursor < offset) {
            auto answer = page(s, ex, cursor);
            cursor += answer[21];
        }
        if (cursor != offset)
            return fault(ex, 7);
        auto answer = page(s, ex, offset);
        s.furthest_end = std::max(s.furthest_end, offset + answer[21]);
        return answer;
    }
    if (op == 4) {
        auto tid = reader.i();
        if (tid != s.transfer_id || s.transfer.empty())
            return fault(ex, 7);
        s.transfer = Bytes{};
        Fields f;
        f.i(tid);
        return packet(0x83, ex, f.span());
    }
    if (s.transfer_id == UINT32_MAX)
        return fault(ex, 9);
    Binding binding = s.binding;
    Json response;
    Command cmd;
    const Map *map = nullptr;
    unsigned ceiling = s.profile == 1 ? 1024 : s.profile == 2 ? 4096 : 8192;
    if (op == 2) {
        unsigned role = reader.b();
        binding.match = reader.str();
        binding.player = reader.j();
        binding.credential = reader.str();
        if (role != 1)
            return fault(ex, 1);
        auto code = server_.check_seat(binding.match, binding.player, binding.credential);
        if (!code.empty())
            return fault(ex, 3);
        map = server_.room_map(binding.match);
        if (profile_bytes(*map) > ceiling)
            return fault(ex, 10);
        auto hint = server_.numeric_key_hint(binding.match, binding.player);
        binding.next_key = hint.first;
        binding.exhausted = hint.second;
        response = Json::object();
        response.set("type", "BOUND");
        response.set("ok", true);
        response.set("match_id", binding.match);
    } else {
        cmd = decode_request(p, binding);
        if ((op == 20 && !binding.player) || ((op == 19 || op >= 21) && cmd.match.empty()))
            return fault(ex, 3);
        map = op == 17 ? server_.registered_map(cmd.json.get_string("map_id").value_or("training-01"))
                       : server_.room_map(cmd.match);
        if (map && (op == 17 || op == 18 || op == 21 || op == 22) && profile_bytes(*map) > ceiling)
            return fault(ex, 10);
    }
    // Reserve both serialized reply storage and global capacity before any mutating dispatch.
    bool mutation = op == 17 || op == 18 || op == 19 || op == 20;
    size_t others = transfer_storage();
    if (others >= limits_.global_bytes)
        return fault(ex, 9);
    size_t allowed = std::min(limits_.transfer_bytes, limits_.global_bytes - others);
    if (mutation && allowed < 4096)
        return fault(ex, 9);
    Records records(mutation ? 4096 : allowed);
    std::uint32_t h = 0;
    if (op != 2) {
        if (op == 16) {
            server_.poll();
            if (server_.registered_map_count() > 65535)
                return fault(ex, 9);
            Fields count;
            count.h(server_.registered_map_count());
            records.add(19, count.span());
            server_.visit_map_info([&](const server::MapInfo &info) {
                Fields item;
                item.i(map_handle(info.id)).str(info.id).b(info.min_players).b(info.max_players);
                records.add(20, item.span());
                records.text(1, info.name);
                records.text(2, info.description);
            });
        } else if (op == 22) {
            server_.poll();
            auto m = server_.find_match(cmd.match);
            if (!map)
                response = server::make_error("UNKNOWN_MATCH", "no such match", &cmd.json);
            else if (!m)
                response = server::make_error("NOT_STARTED", "match has not started", &cmd.json);
            else {
                int since = (int)cmd.json.get_int("since_version").value();
                if (!server_.retained_events_available(cmd.match, since))
                    response = server::make_error("SNAPSHOT_REQUIRED",
                                                  "requested events are no longer retained", &cmd.json);
                else {
                    h = (std::uint32_t)m->state_version();
                    if (since >= 0 && (std::uint32_t)since < h)
                        h = std::min<std::uint32_t>(h, (std::uint32_t)since + cmd.max_versions);
                    Fields ctx;
                    ctx.str(cmd.match);
                    records.add(1, ctx.span());
                    std::uint32_t count = 0;
                    for (const auto &e : m->all_events())
                        if (e.state_version > since && (std::uint32_t)e.state_version <= h)
                            ++count;
                    Fields meta;
                    meta.i((std::uint32_t)since).i(count);
                    records.add(22, meta.span());
                    for (const auto &e : m->all_events())
                        if (e.state_version > since && (std::uint32_t)e.state_version <= h)
                            encode_event(records, event_to_json(e, cmd.match));
                }
            }
        } else
            response = server_.handle(cmd.json);
        if (string(response, "type") == "JOINED") {
            binding.match = string(response, "match_id");
            binding.player = (int)integer(response, "player_id");
            binding.credential = string(response, "session");
            auto hint = server_.numeric_key_hint(binding.match, binding.player);
            binding.next_key = hint.first;
            binding.exhausted = hint.second;
        }
        auto type = string(response, "type");
        if (type == "SNAPSHOT")
            h = integer(*response.find("snapshot"), "state_version");
        else if (type == "MATCH_STATUS")
            h = integer(*response.find("status"), "state_version");
        else if (type == "SUBMIT_PROGRAM")
            h = integer(response, "state_version");
    }
    bool error = string(response, "type") == "ERROR";
    if (!response.is_null())
        encode_response(records, response, map, map ? map_handle(map->id) : 0, &binding,
                        [&](const std::string &id) { return map_handle(id); });
    auto bytes = records.take();
    if (bytes.empty())
        throw CodecError("empty response");
    // Retain immutable serialized bytes; FETCH never re-enters the engine or JSON serializer.
    s.transfer = std::move(bytes);
    ++s.transfer_id;
    s.high_water = h;
    s.origin = op;
    s.error = error;
    s.furthest_end = 0;
    if (op == 2 || string(response, "type") == "JOINED")
        s.binding = std::move(binding);
    auto answer = page(s, ex, 0);
    s.furthest_end = answer[21];
    return answer;
}
} // namespace botched::binary
