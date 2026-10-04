#include "feature.hpp"
#include "../gui/config.hpp"
#include "../gui/menu.hpp"
#include "../core/math.hpp"
#include "../sdk/input.hpp"
#include "../sdk/entity_system.hpp"
#include "../sdk/pawn.hpp"
#include "../sdk/offsets.hpp"
#include "../hooks/hooks.hpp"
#include "dearimgui.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>

static constexpr uintptr_t OFF_V_ANGLE      = 0x1330;
static constexpr uintptr_t OFF_V_ANGLE_PREV = 0x133C;
static constexpr uintptr_t OFF_ABS_VEL      = 0x568;
static constexpr uintptr_t OFF_PUNCH_SVC    = 0x1520;
static constexpr uintptr_t OFF_PUNCH_ANG    = 0x50;
static constexpr uintptr_t OFF_IS_SCOPED    = 0x2D30;
static constexpr uintptr_t OFF_SPOTTED      = 0x2D18;
static constexpr uintptr_t OFF_SPOTTED_MASK = 0xC;
static constexpr uintptr_t OFF_ACC_PENALTY  = 0x28A8;
static constexpr uintptr_t OFF_TURN_INACC   = 0x28A4;
static constexpr uintptr_t OFF_ZOOM_LEVEL   = 0x2DA8;
static constexpr uintptr_t OFF_WPN_MODE     = 0x2890;

enum { HG_HEAD = 1, HG_CHEST = 2 };
enum { ST_IDLE = 0, ST_SNAP = 1, ST_FIRE = 2, ST_HOLD = 3 };

static const int ESP_PAIRS[][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 6},
    {4, 8}, {8, 9}, {9, 10},
    {4, 13}, {13, 14}, {14, 15},
    {0, 22}, {22, 23}, {23, 24},
    {0, 25}, {25, 26}, {26, 27},
};
static constexpr int ESP_PAIR_N = (int)(sizeof(ESP_PAIRS) / sizeof(ESP_PAIRS[0]));

struct WpnData {
    int   damage = 0;
    float hs_mult = 1.f;
    float armor_ratio = 1.f;
    float range = 8192.f;
    float range_mod = 0.98f;
    int   type = -1;
    int   clip = -999;
    int   zoom = 0;
    int   mode = 0;
    float inacc = 0.f;
    float turn = 0.f;
    bool  ok = false;
    bool  fallback = false;
    bool  sniper = false;
    void* wep = nullptr;
    void* vd  = nullptr;
};

struct Spot {
    Vec3  pos{};
    float dmg = 0.f;
    int   hg = 0;
    bool  ok = false;
};

static int      g_state = ST_IDLE;
static unsigned g_ran_tick = 0xFFFFFFFFu;
static unsigned g_state_tick = 0;
static unsigned g_cool_until = 0;
static Vec3     g_saved{};
static Vec3     g_fire_ang{};
static bool     g_have_saved = false;
static bool     g_lmb = false;

static int   g_tgt_handle = -1;
static Vec3  g_aim_pos{};
static Vec3  g_line_a{};
static Vec3  g_hi_pos{};
static int   g_line_ia = -1, g_line_ib = -1, g_hi_bone = -1;
static float g_aim_dmg = 0.f;
static int   g_aim_hg = 0;
static char  g_dbg[256] = "-";
static int   g_dbg_scan = 0, g_dbg_spot = 0, g_dbg_ok = 0;
static int   g_local_idx = 0;
static WpnData g_wpn{};
static Vec3  g_punch{};

static bool  opt_recoil     = false;
static float opt_rcs        = 2.f;
static bool  opt_predict    = false;
static bool  opt_ground     = true;
static bool  opt_need_scope = true;
static bool  opt_wait_acc   = true;
static float opt_max_inacc  = 0.04f;
static int   opt_snap       = 2;
static int   opt_hold       = 3;
static float opt_head_lerp  = 0.50f;
static bool  opt_gun_dot    = true;

static bool bone_ok(const Vec3& b, const Vec3& o) {
    if (b.x == 0.f && b.y == 0.f && b.z == 0.f) return false;
    if (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.z)) return false;
    float dx = b.x - o.x, dy = b.y - o.y, dz = b.z - o.z;
    float d2 = dx * dx + dy * dy + dz * dz;
    return d2 > 1.f && d2 < 200.f * 200.f;
}

static Vec3 lerp3(const Vec3& a, const Vec3& b, float t) {
    return Vec3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

static float clampf(float v, float a, float b) {
    if (v < a) return a;
    if (v > b) return b;
    return v;
}

static bool looks_ang(const Vec3& v) {
    return v.x >= -89.2f && v.x <= 89.2f && fabsf(v.z) < 0.05f &&
           fabsf(v.y) <= 361.f && std::isfinite(v.x) && std::isfinite(v.y);
}

static float ang_fov(const Vec3& a, const Vec3& b) {
    return hypotf(remainderf(a.x - b.x, 360.f), remainderf(a.y - b.y, 360.f));
}

static Vec3 calc_angle(const Vec3& src, const Vec3& dst) {
    Vec3 d = dst - src;
    float hyp = sqrtf(d.x * d.x + d.y * d.y);
    if (hyp < 0.001f) hyp = 0.001f;
    Vec3 a{-atan2f(d.z, hyp) * radpi, atan2f(d.y, d.x) * radpi, 0.f};
    if (a.x > 89.f) a.x = 89.f;
    else if (a.x < -89.f) a.x = -89.f;
    a.y = remainderf(a.y, 360.f);
    return a;
}

static Vec3 ang_fwd(const Vec3& a) {
    float p = a.x * pideg, y = a.y * pideg;
    float cp = cosf(p), sp = sinf(p), cy = cosf(y), sy = sinf(y);
    return Vec3{cp * cy, cp * sy, -sp};
}

static bool pawn_alive(Pawn* p) {
    if (!valid_ptr(p)) return false;
    if (!valid_ptr(p->scene_node())) return false;
    if (p->get_lifestate()) return false;
    int hp = p->get_health();
    if (hp <= 0 || hp > 200) return false;
    int team = (int)p->get_cs_team();
    if (team != 2 && team != 3) return false;
    Vec3 o = p->get_abs_origin();
    if (!std::isfinite(o.x) || !std::isfinite(o.y) || !std::isfinite(o.z)) return false;
    if (o.x == 0.f && o.y == 0.f && o.z == 0.f) return false;
    return true;
}

static Vec3 local_eye(Pawn* local) {
    Vec3 o = local->get_abs_origin();
    Vec3 e = local->get_eye_position();
    float dz = e.z - o.z;
    if (!std::isfinite(e.x) || dz < 40.f || dz > 90.f)
        return Vec3{o.x, o.y, o.z + 64.f};
    return e;
}

static bool targetable(Pawn* pawn, Pawn* local, int local_team) {
    if (!pawn_alive(pawn) || pawn == local) return false;
    if (pawn->is_dormant()) return false;
    int team = (int)pawn->get_cs_team();
    if ((local_team == 2 || local_team == 3) && team == local_team) return false;
    return true;
}

static int ctrl_index(Entity* ctrl) {
    if (!entity_system || !valid_ptr(ctrl)) return 0;
    for (unsigned i = 1; i <= 64; ++i)
        if (entity_system->entity_from_index(i) == ctrl) return (int)i;
    return 0;
}

static int pawn_handle_of(Pawn* p) {
    if (!entity_system || !valid_ptr(p)) return -1;
    for (unsigned i = 1; i <= 64; ++i) {
        Entity* e = entity_system->entity_from_index(i);
        if (!valid_ptr(e)) continue;
        int h = e->get_pawn_handle();
        if (h == -1) continue;
        if (entity_system->pawn_from_pawn_handle(h) == p) return h;
    }
    return -1;
}

static Pawn* resolve_tgt() {
    if (g_tgt_handle == -1 || !entity_system) return nullptr;
    Pawn* p = entity_system->pawn_from_pawn_handle(g_tgt_handle);
    if (!pawn_alive(p)) return nullptr;
    return p;
}

static void drop_tgt() {
    g_tgt_handle = -1;
    target_pawn = nullptr;
}

static bool spotted_by_me(Pawn* enemy, int local_idx) {
    if (!valid_ptr(enemy) || local_idx < 1 || local_idx > 64) return false;
    int bit = local_idx - 1;
    uint32_t* mask = (uint32_t*)((uintptr_t)enemy + OFF_SPOTTED + OFF_SPOTTED_MASK);
    return (mask[bit >> 5] & (1u << (bit & 31))) != 0;
}

static Vec3 abs_vel(void* e) {
    if (!valid_ptr(e)) return {};
    Vec3 v = *(Vec3*)((uintptr_t)e + OFF_ABS_VEL);
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return {};
    if (v.length() > 4000.f) return {};
    return v;
}

static Vec3 predict(Pawn* p, const Vec3& pos) {
    if (!opt_predict) return pos;
    Vec3 v = abs_vel(p);
    if (v.length() < 8.f) return pos;
    return pos + v * (1.f / 64.f);
}

static Vec3 read_punch(Pawn* local) {
    void* svc = *(void**)((uintptr_t)local + OFF_PUNCH_SVC);
    if (!valid_ptr(svc)) return {};
    Vec3 p = *(Vec3*)((uintptr_t)svc + OFF_PUNCH_ANG);
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return {};
    if (fabsf(p.x) > 25.f || fabsf(p.y) > 40.f) return {};
    return p;
}

static bool is_scoped(Pawn* local) {
    return *(bool*)((uintptr_t)local + OFF_IS_SCOPED);
}

static void* active_weapon(Pawn* local) {
    void* ws = local->weapon_services();
    if (!valid_ptr(ws) || !entity_system) return nullptr;
    int handle = *(int*)((uintptr_t)ws + off::ws_hActiveWeapon);
    unsigned idx = (unsigned)(handle & 0x7FFF);
    if (idx == 0 || idx > 0x4000) return nullptr;
    Entity* e = entity_system->entity_from_index(idx);
    return valid_ptr(e) ? e : nullptr;
}

static bool vd_sane(void* vd, WpnData* w) {
    if (!valid_ptr(vd)) return false;
    int   d = *(int*)  ((uintptr_t)vd + off::vd_nDamage);
    float h = *(float*)((uintptr_t)vd + off::vd_flHeadshotMultiplier);
    float a = *(float*)((uintptr_t)vd + off::vd_flArmorRatio);
    float r = *(float*)((uintptr_t)vd + off::vd_flRange);
    float m = *(float*)((uintptr_t)vd + off::vd_flRangeModifier);
    int   t = *(int*)  ((uintptr_t)vd + off::vd_WeaponType);
    if (d < 1 || d > 500) return false;
    if (!std::isfinite(h) || h < 0.2f || h > 8.f) h = 1.f;
    if (!std::isfinite(a) || a < 0.1f || a > 4.f) a = 1.f;
    if (!std::isfinite(r) || r < 100.f || r > 20000.f) r = 8192.f;
    if (!std::isfinite(m) || m < 0.5f || m > 1.f) m = 0.98f;
    w->vd = vd; w->damage = d; w->hs_mult = h; w->armor_ratio = a;
    w->range = r; w->range_mod = m; w->type = t;
    return true;
}

static WpnData read_weapon(Pawn* local) {
    WpnData w;
    w.wep = active_weapon(local);
    if (valid_ptr(w.wep)) {
        w.clip  = *(int*)((uintptr_t)w.wep + off::m_iClip1);
        w.zoom  = *(int*)((uintptr_t)w.wep + OFF_ZOOM_LEVEL);
        w.mode  = *(int*)((uintptr_t)w.wep + OFF_WPN_MODE);
        w.inacc = *(float*)((uintptr_t)w.wep + OFF_ACC_PENALTY);
        w.turn  = *(float*)((uintptr_t)w.wep + OFF_TURN_INACC);
        if (!std::isfinite(w.inacc) || w.inacc < 0.f || w.inacc > 5.f) w.inacc = 0.f;
        if (!std::isfinite(w.turn)  || w.turn  < 0.f || w.turn  > 5.f) w.turn  = 0.f;
        if (w.zoom < 0 || w.zoom > 2) w.zoom = 0;
        if (w.mode < 0 || w.mode > 1) w.mode = 0;
        void* cands[] = {
            *(void**)((uintptr_t)w.wep + off::m_nSubclassID + 8),
            *(void**)((uintptr_t)w.wep + off::m_nSubclassID),
        };
        for (void* vd : cands) {
            if (!vd_sane(vd, &w)) continue;
            w.ok = true;
            break;
        }
    }
    if (!w.ok) {
        w.damage = 35; w.hs_mult = 1.f; w.armor_ratio = 1.f;
        w.range = 8192.f; w.range_mod = 0.98f; w.type = 3;
        w.fallback = true; w.ok = true;
    }
    w.sniper = (w.type == 5) || (w.damage >= 80);
    return w;
}

static float scale_hg(float dmg, int hg, float hs) {
    float mul = (hs < 2.f) ? (4.f * hs) : hs;
    return (hg == HG_HEAD) ? dmg * mul : dmg;
}

static float apply_armor(float dmg, int hg, Pawn* t, float ratio) {
    int armor = t->get_armor();
    if (armor <= 0) return dmg;
    if (hg == HG_HEAD && !t->has_helmet()) return dmg;
    float new_dmg = dmg * (ratio * 0.5f);
    float armor_dmg = (dmg - new_dmg) * 0.5f;
    if (armor_dmg > (float)armor)
        new_dmg = dmg - (float)armor / 0.5f;
    return new_dmg < 0.f ? 0.f : new_dmg;
}

static float calc_dmg(const Vec3& src, const Vec3& dst, Pawn* t, const WpnData& w, int hg) {
    float dist = (dst - src).length();
    if (dist > w.range) return 0.f;
    float dmg = (float)w.damage * powf(w.range_mod, dist * 0.002f);
    return apply_armor(scale_hg(dmg, hg, w.hs_mult), hg, t, w.armor_ratio);
}

static bool esp_head_line(Pawn* p, Vec3* lo, Vec3* hi, int* ia, int* ib) {
    Vec3 o = p->get_abs_origin();
    float best = -1.e9f;
    bool found = false;
    Vec3 ba{}, bb{};
    int ia_b = -1, ib_b = -1;
    for (int i = 0; i < ESP_PAIR_N; ++i) {
        int a = ESP_PAIRS[i][0], b = ESP_PAIRS[i][1];
        Vec3 pa = p->get_bone_location((unsigned)a);
        Vec3 pb = p->get_bone_location((unsigned)b);
        if (!bone_ok(pa, o) || !bone_ok(pb, o)) continue;
        float z = 0.5f * (pa.z + pb.z);
        if (z <= best) continue;
        best = z; ba = pa; bb = pb; ia_b = a; ib_b = b; found = true;
    }
    if (!found) return false;
    if (bb.z < ba.z) { Vec3 t = ba; ba = bb; bb = t; int ti = ia_b; ia_b = ib_b; ib_b = ti; }
    if (lo) *lo = ba; if (hi) *hi = bb;
    if (ia) *ia = ia_b; if (ib) *ib = ib_b;
    return true;
}

static Vec3 highest_bone(Pawn* p, int* idx) {
    Vec3 o = p->get_abs_origin();
    Vec3 best{}; int bi = -1; float bz = -1.e9f;
    for (int i = 0; i <= 32; ++i) {
        Vec3 b = p->get_bone_location((unsigned)i);
        if (!bone_ok(b, o)) continue;
        if (hypotf(b.x - o.x, b.y - o.y) > 28.f) continue;
        if (b.z <= bz) continue;
        bz = b.z; best = b; bi = i;
    }
    if (idx) *idx = bi;
    return best;
}

static Vec3 head_point(Pawn* p) {
    Vec3 o = p->get_abs_origin();
    Vec3 lo, hi; int ia = -1, ib = -1;
    bool line = esp_head_line(p, &lo, &hi, &ia, &ib);
    int hidx = -1; Vec3 hb = highest_bone(p, &hidx);
    g_line_a = lo; g_line_ia = ia; g_line_ib = ib;
    g_hi_bone = hidx; g_hi_pos = hb;
    float t = clampf(opt_head_lerp, 0.f, 1.f);
    if (line && hidx >= 0) return lerp3(lo, hb, t);
    if (hidx >= 0) return hb;
    if (line) return lerp3(lo, hi, t);
    Vec3 e = p->get_eye_position();
    float dz = e.z - o.z;
    if (std::isfinite(e.x) && dz > 40.f && dz < 90.f && bone_ok(e, o)) return e;
    return Vec3{o.x, o.y, o.z + 64.f};
}

static Vec3 chest_point(Pawn* p) {
    Vec3 o = p->get_abs_origin();
    Vec3 a = p->get_bone_location(3), b = p->get_bone_location(4);
    if (bone_ok(a, o) && bone_ok(b, o)) return lerp3(a, b, 0.5f);
    if (bone_ok(a, o)) return a;
    if (bone_ok(b, o)) return b;
    return Vec3{o.x, o.y, o.z + 52.f};
}

static Spot pick_spot(Pawn* p, const Vec3& eye, const WpnData& w, int min_dmg) {
    Spot head, chest;
    head.pos = predict(p, head_point(p));
    head.hg = HG_HEAD;
    head.dmg = calc_dmg(eye, head.pos, p, w, HG_HEAD);
    head.ok = head.dmg >= 1.f;
    chest.pos = predict(p, chest_point(p));
    chest.hg = HG_CHEST;
    chest.dmg = calc_dmg(eye, chest.pos, p, w, HG_CHEST);
    chest.ok = chest.dmg >= 1.f;
    int hp = p->get_health();
    if (head.ok && (head.dmg >= (float)min_dmg || head.dmg >= (float)hp)) return head;
    if (chest.ok && (chest.dmg >= (float)min_dmg || chest.dmg >= (float)hp)) return chest;
    return {};
}

static void write_ang(Pawn* local, const Vec3& ang) {
    if (!looks_ang(ang) || !valid_ptr(local) || !input) return;
    input->set_view_angles(ang, local->get_v_angle());
    *(Vec3*)((uintptr_t)local + OFF_V_ANGLE)      = ang;
    *(Vec3*)((uintptr_t)local + OFF_V_ANGLE_PREV) = ang;
}

static Vec3 with_rcs(Vec3 ang) {
    if (!opt_recoil) return ang;
    ang.x -= g_punch.x * opt_rcs;
    ang.y -= g_punch.y * opt_rcs;
    if (ang.x > 89.f) ang.x = 89.f;
    else if (ang.x < -89.f) ang.x = -89.f;
    ang.y = remainderf(ang.y, 360.f);
    return ang;
}

static void lmb(bool down) {
    if (g_lmb == down) return;
    g_lmb = down;
    aim_inject_lmb(down);
}

static void fire_down(Pawn* local) {
    if (valid_ptr(local)) local->set_button(IN_ATTACK, true);
    lmb(true);
}
static void fire_up(Pawn* local) {
    if (valid_ptr(local)) local->set_button(IN_ATTACK, false);
    lmb(false);
}

static void restore_now(Pawn* local) {
    fire_up(local);
    if (g_have_saved && config.ragebot.restore_view && valid_ptr(local) && looks_ang(g_saved))
        write_ang(local, g_saved);
    g_have_saved = false;
    g_state = ST_IDLE;
    drop_tgt();
}

static bool gun_can_fire(const WpnData& w, Pawn* local) {
    if (w.clip <= 0) return false;
    if (w.type <= 0 || w.type == 7 || w.type == 9 || w.type >= 12) return false;
    if (opt_need_scope && w.sniper) {
        bool sc = is_scoped(local) || w.zoom >= 1 || w.mode == 1;
        if (!sc) return false;
    }
    if (opt_wait_acc && w.sniper) {
        if (w.inacc + w.turn > opt_max_inacc) return false;
    }
    return true;
}

static unsigned cool_ticks(const WpnData& w) {
    if (w.sniper) return 16;
    if (w.type == 1) return 8;
    return 6;
}

static bool still_good(Pawn* pawn, Pawn* local, int local_team) {
    if (!targetable(pawn, local, local_team)) return false;
    if (!config.ragebot.autowall && !spotted_by_me(pawn, g_local_idx)) return false;
    return true;
}

static bool aim_at(Pawn* local, Pawn* pawn, const Vec3& eye, const WpnData& w, int min_dmg) {
    Spot s = pick_spot(pawn, eye, w, min_dmg);
    if (!s.ok) return false;
    g_aim_pos = s.pos;
    g_aim_dmg = s.dmg;
    g_aim_hg  = s.hg;
    g_fire_ang = with_rcs(calc_angle(eye, s.pos));
    write_ang(local, g_fire_ang);
    return true;
}

static void run_rage() {
    if (g_ran_tick == g_cm_ticks) return;
    g_ran_tick = g_cm_ticks;
    g_dbg_scan = g_dbg_spot = g_dbg_ok = 0;

    Pawn* local = (entity_system && valid_ptr(entity_system))
        ? entity_system->get_localpawn() : nullptr;

    if (!config.ragebot.master) {
        if (g_state != ST_IDLE) restore_now(local);
        drop_tgt();
        snprintf(g_dbg, sizeof(g_dbg), "off");
        return;
    }
    if (!pawn_alive(local) || !input) {
        if (g_state != ST_IDLE) restore_now(local);
        snprintf(g_dbg, sizeof(g_dbg), "no local");
        return;
    }

    g_wpn = read_weapon(local);
    g_punch = read_punch(local);

    Entity* localentity = entity_system->get_localentity();
    g_local_idx = ctrl_index(localentity);
    int local_team = (int)local->get_cs_team();
    Vec3 eye = local_eye(local);
    Vec3 view = input->get_view_angles(local->get_v_angle());
    int min_dmg = config.ragebot.min_damage < 1 ? 1 : config.ragebot.min_damage;
    int snap_n = opt_snap < 1 ? 1 : (opt_snap > 8 ? 8 : opt_snap);
    int hold_n = opt_hold < 1 ? 1 : (opt_hold > 8 ? 8 : opt_hold);

    Pawn* tgt = resolve_tgt();
    target_pawn = tgt;

    if (g_state == ST_SNAP || g_state == ST_FIRE || g_state == ST_HOLD) {
        if (!tgt || !still_good(tgt, local, local_team) ||
            !aim_at(local, tgt, eye, g_wpn, min_dmg)) {
            restore_now(local);
            g_cool_until = g_cm_ticks + 8;
            snprintf(g_dbg, sizeof(g_dbg), "drop (dead/lost)");
            return;
        }

        if (g_state == ST_SNAP) {
            fire_up(local);
            if (g_cm_ticks - g_state_tick >= (unsigned)snap_n) {
                g_state = ST_FIRE;
                g_state_tick = g_cm_ticks;
            }
            snprintf(g_dbg, sizeof(g_dbg), "SNAP %s left=%u inacc=%.3f",
                     g_aim_hg == HG_HEAD ? "HEAD" : "CHEST",
                     (unsigned)snap_n - (g_cm_ticks - g_state_tick),
                     g_wpn.inacc + g_wpn.turn);
            return;
        }

        if (g_state == ST_FIRE) {
            if (config.ragebot.auto_shoot) fire_down(local);
            if (g_cm_ticks - g_state_tick >= 1) {
                fire_up(local);
                g_state = ST_HOLD;
                g_state_tick = g_cm_ticks;
            }
            snprintf(g_dbg, sizeof(g_dbg), "FIRE %s dmg=%.0f",
                     g_aim_hg == HG_HEAD ? "HEAD" : "CHEST", g_aim_dmg);
            return;
        }

        fire_up(local);
        if (g_cm_ticks - g_state_tick >= (unsigned)hold_n) {
            restore_now(local);
            g_cool_until = g_cm_ticks + cool_ticks(g_wpn);
            snprintf(g_dbg, sizeof(g_dbg), "restored");
            return;
        }
        snprintf(g_dbg, sizeof(g_dbg), "HOLD %u",
                 (unsigned)hold_n - (g_cm_ticks - g_state_tick));
        return;
    }

    drop_tgt();

    Pawn* best = nullptr;
    int   best_h = -1;
    Spot  best_s{};
    float best_score = -1.e9f;
    float max_fov = config.ragebot.fov < 0.1f ? 0.1f : config.ragebot.fov;

    for (unsigned i = 1; i <= 64; ++i) {
        Entity* entity = entity_system->entity_from_index(i);
        if (!valid_ptr(entity) || entity == localentity) continue;
        int handle = entity->get_pawn_handle();
        if (handle == -1) continue;
        unsigned idx = (unsigned)(handle & 0x7FFF);
        if (idx == 0 || idx > 0x4000) continue;
        Pawn* pawn = entity_system->pawn_from_pawn_handle(handle);
        if (!targetable(pawn, local, local_team)) continue;
        ++g_dbg_scan;

        bool vis = spotted_by_me(pawn, g_local_idx);
        if (vis) ++g_dbg_spot;
        if (!vis && !config.ragebot.autowall) continue;

        Spot s = pick_spot(pawn, eye, g_wpn, min_dmg);
        if (!s.ok) continue;
        float dist = (s.pos - eye).length();
        if (dist > config.ragebot.max_dist) continue;
        float fov = ang_fov(view, calc_angle(eye, s.pos));
        if (max_fov < 179.f && fov > max_fov) continue;
        ++g_dbg_ok;

        float score = s.dmg - dist * 0.01f - fov * 0.25f;
        if (vis) score += 4000.f;
        if (s.hg == HG_HEAD) score += 200.f;
        if (s.dmg >= (float)pawn->get_health()) score += 1000.f;
        if (score > best_score) {
            best_score = score;
            best = pawn;
            best_h = handle;
            best_s = s;
        }
    }

    if (!best || !best_s.ok || best_h == -1) {
        snprintf(g_dbg, sizeof(g_dbg), "scan:%d spot:%d ok:%d clip=%d",
                 g_dbg_scan, g_dbg_spot, g_dbg_ok, g_wpn.clip);
        return;
    }

    g_tgt_handle = best_h;
    target_pawn = best;
    g_aim_pos = best_s.pos;
    g_aim_dmg = best_s.dmg;
    g_aim_hg = best_s.hg;

    const char* hg = (g_aim_hg == HG_HEAD) ? "HEAD" : "CHEST";

    if (menu_focused) {
        snprintf(g_dbg, sizeof(g_dbg), "READY %s (menu)", hg);
        return;
    }
    if (g_cm_ticks < g_cool_until) {
        snprintf(g_dbg, sizeof(g_dbg), "READY %s cool %u", hg, g_cool_until - g_cm_ticks);
        return;
    }
    if (!gun_can_fire(g_wpn, local)) {
        snprintf(g_dbg, sizeof(g_dbg),
                 "WAIT %s scoped=%d zoom=%d inacc=%.3f clip=%d",
                 hg, (int)is_scoped(local), g_wpn.zoom, g_wpn.inacc + g_wpn.turn, g_wpn.clip);
        return;
    }
    if (opt_ground && !local->on_ground()) {
        snprintf(g_dbg, sizeof(g_dbg), "READY %s (air)", hg);
        return;
    }
    if (abs_vel(local).length() > 140.f && g_wpn.sniper) {
        snprintf(g_dbg, sizeof(g_dbg), "READY %s (moving)", hg);
        return;
    }

    g_saved = view;
    g_have_saved = looks_ang(view);
    g_fire_ang = with_rcs(calc_angle(eye, best_s.pos));
    write_ang(local, g_fire_ang);
    fire_up(local);
    g_state = ST_SNAP;
    g_state_tick = g_cm_ticks;
    snprintf(g_dbg, sizeof(g_dbg), "LOCK %s dmg=%.0f — aiming, no shot yet", hg, g_aim_dmg);
}

static void hold_fire_ang() {
    if (g_state == ST_IDLE) return;
    if (!entity_system || !valid_ptr(entity_system) || !input) return;
    Pawn* local = entity_system->get_localpawn();
    if (!pawn_alive(local) || !looks_ang(g_fire_ang)) return;
    write_ang(local, g_fire_ang);
    if (g_state == ST_FIRE && config.ragebot.auto_shoot)
        fire_down(local);
}

static void draw_dot(ImDrawList* dl, const Vec3& w, ImU32 col, float r) {
    Vec3 s;
    if (!world_to_screen(w, &s)) return;
    dl->AddCircleFilled(ImVec2(s.x, s.y), r, col);
}

struct RageBotFeature final : IFeature {
    const char* name() const override { return "RageBot"; }
    const char* tab()  const override { return "RageBot"; }

    void on_create_move_pre() override { run_rage(); }
    void on_create_move()     override { hold_fire_ang(); }

    void on_draw() override {
        if (!config.ragebot.master) return;
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        if (!dl) return;

        Pawn* tgt = resolve_tgt();
        if (config.ragebot.draw_aim && tgt) {
            Vec3 lo, hi; int ia = -1, ib = -1;
            bool line = esp_head_line(tgt, &lo, &hi, &ia, &ib);
            int hidx = -1; Vec3 hb = highest_bone(tgt, &hidx);
            g_line_a = lo; g_line_ia = ia; g_line_ib = ib;
            g_hi_bone = hidx; g_hi_pos = hb;

            if (line) {
                Vec3 sa;
                if (world_to_screen(lo, &sa))
                    dl->AddCircleFilled(ImVec2(sa.x, sa.y), 4.5f, IM_COL32(80, 220, 255, 255));
            }
            if (hidx >= 0) draw_dot(dl, hb, IM_COL32(80, 255, 80, 255), 4.5f);

            Vec3 aim = g_aim_pos;
            if (line && hidx >= 0)
                aim = lerp3(lo, hb, clampf(opt_head_lerp, 0.f, 1.f));
            else if (hidx >= 0) aim = hb;
            g_aim_pos = aim;
            draw_dot(dl, aim,
                     (g_aim_hg == HG_HEAD) ? IM_COL32(255, 40, 40, 255)
                                           : IM_COL32(255, 180, 40, 255), 6.f);

            if (opt_gun_dot && entity_system && input) {
                Pawn* loc = entity_system->get_localpawn();
                if (pawn_alive(loc)) {
                    Vec3 eye = local_eye(loc);
                    Vec3 va = (g_state != ST_IDLE) ? g_fire_ang
                              : input->get_view_angles(loc->get_v_angle());
                    float dist = (aim - eye).length();
                    if (dist < 8.f) dist = 8.f;
                    draw_dot(dl, eye + ang_fwd(va) * dist, IM_COL32(255, 255, 255, 255), 4.f);
                }
            }
        }

        char buf[320];
        snprintf(buf, sizeof(buf),
                 "rage scan:%d spot:%d ok:%d st:%d h=%d  %s",
                 g_dbg_scan, g_dbg_spot, g_dbg_ok, g_state, g_tgt_handle, g_dbg);
        dl->AddText(ImVec2(20, 90), IM_COL32(255, 80, 80, 255), buf);
    }

    void on_menu() override {
        ImGui::PushID("ragebot");
        ImGui::Checkbox("RageBot master", &config.ragebot.master);
        ImGui::Checkbox("Auto Shoot", &config.ragebot.auto_shoot);
        ImGui::Checkbox("Autowall (IGNORE vis)", &config.ragebot.autowall);
        ImGui::Checkbox("Restore view after flick", &config.ragebot.restore_view);
        ImGui::Checkbox("Draw aim point", &config.ragebot.draw_aim);
        ImGui::SliderInt("Min damage", &config.ragebot.min_damage, 1, 120);
        ImGui::SliderFloat("FOV", &config.ragebot.fov, 0.1f, 180.f, "%.0f");
        ImGui::SliderFloat("Max distance", &config.ragebot.max_dist, 200.f, 8192.f, "%.0f");
        ImGui::Separator();
        ImGui::SliderFloat("Head blend (0=cyan  1=green)", &opt_head_lerp, 0.f, 1.f, "%.2f");
        ImGui::Checkbox("Draw gun point (white)", &opt_gun_dot);
        ImGui::Separator();
        ImGui::SliderInt("Snap ticks BEFORE shot", &opt_snap, 1, 6);
        ImGui::SliderInt("Hold ticks AFTER shot", &opt_hold, 1, 6);
        ImGui::Checkbox("Require scope (AWP/Scout)", &opt_need_scope);
        ImGui::Checkbox("Wait until accurate", &opt_wait_acc);
        ImGui::SliderFloat("Max inaccuracy", &opt_max_inacc, 0.f, 0.3f, "%.3f");
        ImGui::Separator();
        ImGui::Checkbox("Recoil compensation", &opt_recoil);
        ImGui::SliderFloat("RCS scale", &opt_rcs, 0.f, 2.5f, "%.2f");
        ImGui::Checkbox("Predict 1 tick", &opt_predict);
        ImGui::Checkbox("Only on ground", &opt_ground);
        ImGui::Separator();
        ImGui::Text("st:%d handle:%d scoped:%d inacc:%.3f",
                    g_state, g_tgt_handle, 0, g_wpn.inacc + g_wpn.turn);
        ImGui::TextWrapped("%s", g_dbg);
        ImGui::TextWrapped(
            "No more CCSGOInput scan (that crashed on kill #2). "
            "Target is a handle, dropped the tick they die. "
            "LOCK/SNAP = aim, no click. Then FIRE. Autowall OFF, Aimbot OFF.");
        ImGui::PopID();
    }
};
REGISTER_FEATURE(RageBotFeature);

extern "C" void* rage_keep() { return (void*)&_inst_RageBotFeature; }
