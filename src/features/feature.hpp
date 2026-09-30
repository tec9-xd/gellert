#pragma once
#include <vector>
#include <cstring>

struct IFeature {
    virtual ~IFeature() = default;
    virtual const char* name() const = 0;
    virtual const char* tab()  const = 0;
    virtual void on_init() {}
    virtual void on_create_move_pre() {}
    virtual void on_create_move() {}
    virtual void on_draw() {}
    virtual void on_menu() {}
};

class FeatureRegistry {
public:
    static FeatureRegistry& get() {
        static FeatureRegistry r;
        return r;
    }
    void add(IFeature* f) { feats.push_back(f); }
    void init_all() { for (auto* f : feats) f->on_init(); }
    void create_move_pre() { for (auto* f : feats) f->on_create_move_pre(); }
    void create_move()     { for (auto* f : feats) f->on_create_move(); }
    void draw() { for (auto* f : feats) f->on_draw(); }
    void menu_for_tab(const char* tab) {
        for (auto* f : feats)
            if (!strcmp(f->tab(), tab)) f->on_menu();
    }
    const std::vector<IFeature*>& all() const { return feats; }
private:
    std::vector<IFeature*> feats;
};

#define REGISTER_FEATURE(T) \
    static T _inst_##T; \
    namespace { IFeature* _keep_##T = (_inst_##T.name(), FeatureRegistry::get().add(&_inst_##T), &_inst_##T); }
