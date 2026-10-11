#pragma once

#include <memory>

#include <rex/rex_app.h>

class GeometryWarsRetroEvolved2App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<GeometryWarsRetroEvolved2App>(
        new GeometryWarsRetroEvolved2App(ctx, "geometry_wars_retro_evolved_2", PPCImageConfig));
  }
};
