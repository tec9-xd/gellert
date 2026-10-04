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
static constexpr uintptr_t OFF_ANG_EYE      = 0x4490;
static constexpr uintptr_t OFF_ABS_VEL      = 0x568;
static constexpr uintptr_t OFF_PUNCH_SVC    = 0x1520;
static constexpr uintptr_t OFF_PUNCH_ANG    = 0x50;
static constexpr uintptr_t OFF_SHOTS_FIRED  = 0x2D44;
static constexpr uintptr_t OFF_SPOTTED      = 0x2D18;
static constexpr uintptr_t OFF_SPOTTED_MASK = 0xC;

enum { HG_HEAD = 1, HG_CHEST = 2 };
enum { ST_IDLE = 0, ST_FIRE = 1, ST_HOLD = 2 };

// EXACT pairs esp.cpp bone_line() draws
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
    bool  ok = false;
    bool  fallback = false;
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

static Pawn* g_target = nullptr;
static Vec3  g_aim_pos{};
static Vec3  g_line_a{};
static Vec3  g_line_b{};
static int   g_line_ia = -1;
static int   g_line_ib = -1;
static bool  g_line_ok = false;
static int   g_hi_bone = -1;
static Vec3  g_hi_pos{};
static float g_aim_dmg = 0.f;
static int   g_aim_hg = 0;
static char  g_dbg[256] = "-";
static int   g_dbg_scan = 0;
static int   g_dbg_spot = 0;
static int   g_dbg_ok = 0;
static int   g_local_idx = 0;
static WpnData g_wpn{};
static Vec3  g_punch{};

static bool  opt_recoil    = true;
static float opt_rcs       = 2.f;
static bool  opt_predict   = true;
static bool  opt_ground    = true;
static int   opt_hold      = 2;
static float opt_head_lerp = 0.85f; // 0 = lower end of head-line, 1 = higher end
static bool  opt_ids       = true;

static bool bone_ok(const Vec3& b, const Vec3& o) {
    if (b.x == 0.f && b.y == 0.f && b.z == 0.f) return false;
    if (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.z)) return false;
    float dx = b.x - o.x, dy = b.y - o.y, dz = b.z - o.z;
    float d2 = dx * dx + dy * dy + dz * dz;
    return d2 > 1.f && d2 < 200.f * 200.f;
}

static Vec3 lerp3(const Vec3& a, const Vec3& b, float t) {
    return Vec3{
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

static Vec3 local_eye(Pawn* local) {
    Vec3 o = local->get_abs_origin();
    Vec3 e = local->get_eye_position();
    float dz = e.z - o.z;
    if (!std::isfinite(e.x) || dz < 40.f || dz > 90.f)
        return Vec3{o.x, o.y, o.z + 64.f};
    return e;
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

static float ang_fov(const Vec3& a, const Vec3& b) {
    return hypotf(remainderf(a.x - b.x, 360.f), remainderf(a.y - b.y, 360.f));
}

static bool targetable(Pawn* pawn, Pawn* local, int local_team) {
    if (!valid_ptr(pawn) || pawn == local) return false;
    if (pawn->get_lifestate() || pawn->is_dormant()) return false;
    int team = (int)pawn->get_cs_team();
    if (team != 2 && team != 3) return false;
    if ((local_team == 2 || local_team == 3) && team == local_team) return false;
    int hp = pawn->get_health();
    if (hp <= 0 || hp > 200) return false;
    Vec3 o = pawn->get_abs_origin();
    return !(o.x == 0.f && o.y == 0.f && o.z == 0.f);
}

static int ctrl_index(Entity* ctrl) {
    if (!entity_system || !valid_ptr(ctrl)) return 0;
    for (unsigned i = 1; i <= 64; ++i)
        if (entity_system->entity_from_index(i) == ctrl) return (int)i;
    return 0;
}

static bool spotted_by_me(Pawn* enemy, int local_idx) {
    if (!valid_ptr(enemy) || local_idx < 1 || local_idx > 64) return false;
    int bit = local_idx - 1;
    uint32_t* mask = (uint32_t*)((uintptr_t)enemy + OFF_SPOTTED + OFF_SPOTTED_MASK);
    return (mask[bit >> 5] & (1u << (bit & 31))) != 0;
}

static Vec3 abs_vel(void* e) {
    Vec3 v = *(Vec3*)((uintptr_t)e + OFF_ABS_VEL);
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return {};
    if (v.length() > 4000.f) return {};
    return v;
}

static Vec3 predict(Pawn* p, const Vec3& pos) {
    if (!opt_predict) return pos;
    return pos + abs_vel(p) * (1.f / 64.f);
}

static Vec3 read_punch(Pawn* local) {
    void* svc = *(void**)((uintptr_t)local + OFF_PUNCH_SVC);
    if (!valid_ptr(svc)) return {};
    Vec3 p = *(Vec3*)((uintptr_t)svc + OFF_PUNCH_ANG);
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return {};
    if (fabsf(p.x) > 25.f || fabsf(p.y) > 40.f) return {};
    return p;
}

static int shots_fired(Pawn* local) {
    int n = *(int*)((uintptr_t)local + OFF_SHOTS_FIRED);
    if (n < 0 || n > 40) return 0;
    return n;
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
    w->vd = vd;
    w->damage = d;
    w->hs_mult = h;
    w->armor_ratio = a;
    w->range = r;
    w->range_mod = m;
    w->type = t;
    return true;
}

static WpnData read_weapon(Pawn* local) {
    WpnData w;
    w.wep = active_weapon(local);
    if (valid_ptr(w.wep))
        w.clip = *(int*)((uintptr_t)w.wep + off::m_iClip1);
    if (valid_ptr(w.wep)) {
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
        w.damage = 35;
        w.hs_mult = 1.f;
        w.armor_ratio = 1.f;
        w.range = 8192.f;
        w.range_mod = 0.98f;
        w.type = 3;
        w.fallback = true;
        w.ok = true;
    }
    return w;
}

static float scale_hg(float dmg, int hg, float hs) {
    float mul = (hs < 2.f) ? (4.f * hs) : hs;
    if (hg == HG_HEAD) return dmg * mul;
    return dmg;
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
    dmg = scale_hg(dmg, hg, w.hs_mult);
    return apply_armor(dmg, hg, t, w.armor_ratio);
}

// Highest-Z segment among the same pairs ESP draws. That is the diagonal in the skull.
static bool esp_head_line(Pawn* p, Vec3* lo, Vec3* hi, int* ia, int* ib) {
    Vec3 o = p->get_abs_origin();
    float best = -1.e9f;
    bool found = false;
    Vec3 ba_best{}, bb_best{};
    int ia_best = -1, ib_best = -1;
    for (int i = 0; i < ESP_PAIR_N; ++i) {
        int a = ESP_PAIRS[i][0], b = ESP_PAIRS[i][1];
        Vec3 pa = p->get_bone_location((unsigned)a);
        Vec3 pb = p->get_bone_location((unsigned)b);
        if (!bone_ok(pa, o) || !bone_ok(pb, o)) continue;
        float z = 0.5f * (pa.z + pb.z);
        if (z <= best) continue;
        best = z;
        ba_best = pa; bb_best = pb;
        ia_best = a; ib_best = b;
        found = true;
    }
    if (!found) return false;
    if (bb_best.z < ba_best.z) {
        Vec3 t = ba_best; ba_best = bb_best; bb_best = t;
        int ti = ia_best; ia_best = ib_best; ib_best = ti;
    }
    if (lo) *lo = ba_best;
    if (hi) *hi = bb_best;
    if (ia) *ia = ia_best;
    if (ib) *ib = ib_best;
    return true;
}

static Vec3 highest_bone(Pawn* p, int* idx) {
    Vec3 o = p->get_abs_origin();
    Vec3 best{};
    int  bi = -1;
    float bz = -1.e9f;
    for (int i = 0; i <= 64; ++i) {
        Vec3 b = p->get_bone_location((unsigned)i);
        if (!bone_ok(b, o)) continue;
        float xy = hypotf(b.x - o.x, b.y - o.y);
        if (xy > 28.f) continue;
        if (b.z <= bz) continue;
        bz = b.z;
        best = b;
        bi = i;
    }
    if (idx) *idx = bi;
    return best;
}

static Vec3 head_point(Pawn* p) {
    Vec3 o = p->get_abs_origin();
    Vec3 lo, hi;
    int ia = -1, ib = -1;
    bool line = esp_head_line(p, &lo, &hi, &ia, &ib);
    int hidx = -1;
    Vec3 hb = highest_bone(p, &hidx);

    g_line_ok = line;
    g_line_a = lo;
    g_line_b = hi;
    g_line_ia = ia;
    g_line_ib = ib;
    g_hi_bone = hidx;
    g_hi_pos = hb;

    float t = opt_head_lerp;
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;

    if (line) return lerp3(lo, hi, t);
    if (hidx >= 0) return hb;
    Vec3 e = p->get_eye_position();
    float dz = e.z - o.z;
    if (std::isfinite(e.x) && dz > 40.f && dz < 90.f && bone_ok(e, o))
        return e;
    return Vec3{o.x, o.y, o.z + 64.f};
}

static Vec3 chest_point(Pawn* p) {
    Vec3 o = p->get_abs_origin();
    Vec3 a = p->get_bone_location(3);
    Vec3 b = p->get_bone_location(4);
    if (bone_ok(a, o) && bone_ok(b, o))
        return lerp3(a, b, 0.5f);
    if (bone_ok(a, o)) return a;
    if (bone_ok(b, o)) return b;
    return Vec3{o.x, o.y, o.z + 52.f};
}

static Spot pick_spot(Pawn* p, const Vec3& eye, const WpnData& w, int min_dmg) {
    Spot head, chest;
    head.pos = predict(p, head_point(p));
    head.hg  = HG_HEAD;
    head.dmg = calc_dmg(eye, head.pos, p, w, HG_HEAD);
    head.ok  = head.dmg >= 1.f;

    chest.pos = predict(p, chest_point(p));
    chest.hg  = HG_CHEST;
    chest.dmg = calc_dmg(eye, chest.pos, p, w, HG_CHEST);
    chest.ok  = chest.dmg >= 1.f;

    int hp = p->get_health();
    if (head.ok && (head.dmg >= (float)min_dmg || head.dmg >= (float)hp))
        return head;
    if (chest.ok && (chest.dmg >= (float)min_dmg || chest.dmg >= (float)hp))
        return chest;
    return {};
}

static void write_ang(Pawn* local, const Vec3& ang) {
    if (input) input->set_view_angles(ang, valid_ptr(local) ? local->get_v_angle() : Vec3{});
    if (!valid_ptr(local)) return;
    *(Vec3*)((uintptr_t)local + OFF_V_ANGLE)      = ang;
    *(Vec3*)((uintptr_t)local + OFF_V_ANGLE_PREV) = ang;
    *(Vec3*)((uintptr_t)local + OFF_ANG_EYE)      = ang;
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
    if (g_have_saved && config.ragebot.restore_view)
        write_ang(local, g_saved);
    g_have_saved = false;
    g_state = ST_IDLE;
    g_target = nullptr;
    target_pawn = nullptr;
}

static bool gun_can_fire(const WpnData& w) {
    if (w.clip <= 0) return false;
    if (w.type <= 0 || w.type == 7 || w.type == 9 || w.type >= 12) return false;
    return true;
}

static unsigned cool_ticks(const WpnData& w) {
    if (w.type == 5 || w.damage >= 80) return 18;
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
        g_target = nullptr;
        target_pawn = nullptr;
        snprintf(g_dbg, sizeof(g_dbg), "off");
        return;
    }
    if (!valid_ptr(local) || local->get_lifestate() || !input) {
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
    int hold_n = opt_hold < 1 ? 1 : (opt_hold > 6 ? 6 : opt_hold);

    if (g_state == ST_FIRE) {
        if (!still_good(g_target, local, local_team) || !aim_at(local, g_target, eye, g_wpn, min_dmg)) {
            restore_now(local);
            snprintf(g_dbg, sizeof(g_dbg), "lost in fire");
            return;
        }
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

    if (g_state == ST_HOLD) {
        if (valid_ptr(g_target) && still_good(g_target, local, local_team))
            aim_at(local, g_target, eye, g_wpn, min_dmg);
        else
            write_ang(local, g_fire_ang);
        fire_up(local);
        if (g_cm_ticks - g_state_tick >= (unsigned)hold_n) {
            restore_now(local);
            g_cool_until = g_cm_ticks + cool_ticks(g_wpn);
            snprintf(g_dbg, sizeof(g_dbg), "restored");
            return;
        }
        snprintf(g_dbg, sizeof(g_dbg), "HOLD %u", (unsigned)hold_n - (g_cm_ticks - g_state_tick));
        return;
    }

    Pawn* best = nullptr;
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

        Vec3 want = calc_angle(eye, s.pos);
        float fov = ang_fov(view, want);
        if (max_fov < 179.f && fov > max_fov) continue;

        ++g_dbg_ok;
        float score = s.dmg - dist * 0.01f - fov * 0.25f;
        if (vis) score += 4000.f;
        if (s.hg == HG_HEAD) score += 200.f;
        if (s.dmg >= (float)pawn->get_health()) score += 1000.f;
        if (score > best_score) {
            best_score = score;
            best = pawn;
            best_s = s;
        }
    }

    if (!best || !best_s.ok) {
        g_target = nullptr;
        target_pawn = nullptr;
        snprintf(g_dbg, sizeof(g_dbg),
                 "scan:%d spot:%d ok:%d clip=%d type=%d",
                 g_dbg_scan, g_dbg_spot, g_dbg_ok, g_wpn.clip, g_wpn.type);
        return;
    }

    g_target  = best;
    g_aim_pos = best_s.pos;
    g_aim_dmg = best_s.dmg;
    g_aim_hg  = best_s.hg;
    target_pawn = best;
    head_point(best); // refresh line debug for THIS pawn only

    const char* hg = (g_aim_hg == HG_HEAD) ? "HEAD" : "CHEST";

    if (menu_focused) {
        snprintf(g_dbg, sizeof(g_dbg), "READY %s dmg=%.0f pair=%d-%d (menu)",
                 hg, g_aim_dmg, g_line_ia, g_line_ib);
        return;
    }
    if (g_cm_ticks < g_cool_until) {
        snprintf(g_dbg, sizeof(g_dbg), "READY %s cool %u", hg, g_cool_until - g_cm_ticks);
        return;
    }
    if (!gun_can_fire(g_wpn)) {
        snprintf(g_dbg, sizeof(g_dbg), "READY %s nofire clip=%d type=%d", hg, g_wpn.clip, g_wpn.type);
        return;
    }
    if (opt_ground && !local->on_ground()) {
        snprintf(g_dbg, sizeof(g_dbg), "READY %s (air)", hg);
        return;
    }

    g_saved = view;
    g_have_saved = true;
    g_fire_ang = with_rcs(calc_angle(eye, best_s.pos));
    write_ang(local, g_fire_ang);
    if (config.ragebot.auto_shoot)
        fire_down(local);
    g_state = ST_FIRE;
    g_state_tick = g_cm_ticks;
    snprintf(g_dbg, sizeof(g_dbg), "FLICK %s dmg=%.0f pair=%d-%d lerp=%.2f",
             hg, g_aim_dmg, g_line_ia, g_line_ib, opt_head_lerp);
}

static void hold_fire_ang() {
    if (g_state == ST_IDLE) return;
    if (!entity_system || !valid_ptr(entity_system)) return;
    Pawn* local = entity_system->get_localpawn();
    if (!valid_ptr(local)) return;
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

        if (config.ragebot.draw_aim && valid_ptr(g_target)) {
            Vec3 lo, hi;
            int ia = -1, ib = -1;
            bool line = esp_head_line(g_target, &lo, &hi, &ia, &ib);
            g_line_ok = line;
            g_line_a = lo; g_line_b = hi;
            g_line_ia = ia; g_line_ib = ib;
            g_hi_pos = highest_bone(g_target, &g_hi_bone);

            if (opt_ids) {
                Vec3 o = g_target->get_abs_origin();
                for (int i = 0; i <= 32; ++i) {
                    Vec3 b = g_target->get_bone_location((unsigned)i);
                    if (!bone_ok(b, o)) continue;
                    Vec3 s;
                    if (!world_to_screen(b, &s)) continue;
                    char n[8];
                    snprintf(n, sizeof(n), "%d", i);
                    dl->AddCircleFilled(ImVec2(s.x, s.y), 2.4f, IM_COL32(170, 170, 170, 200));
                    dl->AddText(ImVec2(s.x + 3, s.y - 6), IM_COL32(255, 255, 255, 230), n);
                }
            }

            if (line) {
                Vec3 sa, sb;
                if (world_to_screen(lo, &sa) && world_to_screen(hi, &sb)) {
                    dl->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y),
                                IM_COL32(255, 0, 255, 255), 2.5f);
                    dl->AddCircleFilled(ImVec2(sa.x, sa.y), 4.f, IM_COL32(80, 220, 255, 255));
                    dl->AddCircleFilled(ImVec2(sb.x, sb.y), 4.f, IM_COL32(255, 220, 0, 255));
                }
            }
            if (g_hi_bone >= 0)
                draw_dot(dl, g_hi_pos, IM_COL32(80, 255, 80, 255), 4.f);
            draw_dot(dl, g_aim_pos,
                     (g_aim_hg == HG_HEAD) ? IM_COL32(255, 40, 40, 255)
                                           : IM_COL32(255, 180, 40, 255), 6.f);
        }

        char buf[320];
        snprintf(buf, sizeof(buf),
                 "rage scan:%d spot:%d ok:%d pair:%d-%d hi:%d  %s",
                 g_dbg_scan, g_dbg_spot, g_dbg_ok, g_line_ia, g_line_ib, g_hi_bone, g_dbg);
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
        ImGui::Text("Head = highest ESP skeleton edge (same pairs as esp.cpp)");
        ImGui::SliderFloat("Head lerp (0=low  1=high end of that edge)", &opt_head_lerp, 0.f, 1.f, "%.2f");
        ImGui::Checkbox("Draw bone IDs 0-32", &opt_ids);
        ImGui::Separator();
        ImGui::Checkbox("Recoil compensation", &opt_recoil);
        ImGui::SliderFloat("RCS scale", &opt_rcs, 0.f, 2.5f, "%.2f");
        ImGui::Checkbox("Predict 1 tick", &opt_predict);
        ImGui::Checkbox("Only on ground", &opt_ground);
        ImGui::SliderInt("Hold aim ticks after shot", &opt_hold, 1, 5);
        ImGui::Separator();
        ImGui::Text("pair %d -> %d   highest bone %d   lerp %.2f",
                    g_line_ia, g_line_ib, g_hi_bone, opt_head_lerp);
        ImGui::TextWrapped("%s", g_dbg);
        ImGui::TextWrapped(
            "MAGENTA = highest ESP edge (should be the diagonal through the skull). "
            "CYAN = lower end, YELLOW = upper end, GREEN = highest bone, RED = aim. "
            "If magenta matches the ESP head stroke, raise lerp toward 0.9. "
            "Autowall OFF. Aimbot OFF.");
        ImGui::PopID();
    }
};
REGISTER_FEATURE(RageBotFeature);

extern "C" void* rage_keep() { return (void*)&_inst_RageBotFeature; }
