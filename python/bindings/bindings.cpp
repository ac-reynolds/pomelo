#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include "application/application.hpp"
#include <cmath>
#include <limits>
#include <string>
#include <stdexcept>

namespace nb = nanobind;
namespace {
std::string preset(nb::handle value) {
    if (!PyUnicode_Check(value.ptr())) throw nb::type_error("preset: expected a string");
    const auto result = nb::cast<std::string>(value);
    if (result != "smoke" && result != "taylor_green" && result != "double_shear_layer" && result != "vortex_ring")
        throw nb::value_error("preset: expected smoke, taylor_green, double_shear_layer, or vortex_ring");
    return result;
}

bool boolean(nb::handle value, const char* path) {
    if (!PyBool_Check(value.ptr())) throw nb::type_error((std::string(path) + ": expected bool").c_str());
    return value.ptr() == Py_True;
}

uint32_t iterations(nb::handle value) {
    if (PyBool_Check(value.ptr()) || !PyLong_Check(value.ptr()))
        throw nb::type_error("pressure_iterations: expected an integer");
    const auto result = PyLong_AsUnsignedLong(value.ptr());
    if (PyErr_Occurred()) {
        PyErr_Clear();
        throw nb::value_error("pressure_iterations: must be between 1 and 4096");
    }
    if (result < 1 || result > 4096) throw nb::value_error("pressure_iterations: must be between 1 and 4096");
    return static_cast<uint32_t>(result);
}

float number(nb::handle value, const std::string& path) {
    if (PyBool_Check(value.ptr()) || (!PyLong_Check(value.ptr()) && !PyFloat_Check(value.ptr())))
        throw nb::type_error((path + ": expected an integer or float").c_str());
    const double result = PyFloat_AsDouble(value.ptr());
    if (PyErr_Occurred()) {
        PyErr_Clear();
        throw nb::value_error((path + ": exceeds simulator numeric range").c_str());
    }
    if (!std::isfinite(result) || std::abs(result) > std::numeric_limits<float>::max())
        throw nb::value_error((path + ": must be finite and fit float32").c_str());
    return static_cast<float>(result);
}

std::vector<std::array<float, 3>> sources(nb::handle value) {
    if (value.is_none()) return application::default_settings().sources;
    if (!PySequence_Check(value.ptr()) || PyUnicode_Check(value.ptr()) || PyBytes_Check(value.ptr()))
        throw nb::type_error("sources: expected a sequence of coordinate triples");
    nb::object sequence = nb::steal<nb::object>(PySequence_Fast(value.ptr(), "sources: expected a sequence"));
    if (!sequence.is_valid()) throw nb::python_error();
    std::vector<std::array<float, 3>> result;
    const auto count = PySequence_Fast_GET_SIZE(sequence.ptr());
    result.reserve(count);
    for (Py_ssize_t i = 0; i < count; ++i) {
        nb::handle item(PySequence_Fast_GET_ITEM(sequence.ptr(), i));
        const std::string path = "sources[" + std::to_string(i) + "]";
        if (!PySequence_Check(item.ptr()) || PyUnicode_Check(item.ptr()) || PyBytes_Check(item.ptr()))
            throw nb::type_error((path + ": expected a coordinate triple").c_str());
        nb::object triple = nb::steal<nb::object>(PySequence_Fast(item.ptr(), "expected a coordinate triple"));
        if (!triple.is_valid()) throw nb::python_error();
        if (PySequence_Fast_GET_SIZE(triple.ptr()) != 3)
            throw nb::value_error((path + ": expected exactly three coordinates").c_str());
        std::array<float, 3> center;
        for (size_t axis = 0; axis < 3; ++axis)
            center[axis] = number(nb::handle(PySequence_Fast_GET_ITEM(triple.ptr(), axis)), path + "." + "xyz"[axis]);
        result.push_back(center);
    }
    return result;
}

nb::list source_list(const application::Settings& settings) {
    nb::list result;
    for (const auto& center : settings.sources)
        result.append(nb::make_tuple(center[0], center[1], center[2]));
    return result;
}

void check_signals() {
    nb::gil_scoped_acquire acquire;
    if (PyErr_CheckSignals() != 0) throw nb::python_error();
}
}

NB_MODULE(_native, m) {
    nb::class_<application::Settings>(m, "Settings", "Editable simulation configuration; sources are copied on access.")
        .def("__init__", [](application::Settings* self, nb::object opacity, nb::object input_sources,
            nb::object input_preset, nb::object thermal, nb::object dissipation, nb::object yaw, nb::object pitch,
            nb::object zoom, nb::object pressure_budget) {
            auto settings = application::default_settings();
            settings.domain_box_opacity = number(opacity, "domain_box_opacity");
            settings.sources = sources(input_sources);
            settings.preset = preset(input_preset);
            settings.thermal_buoyancy = boolean(thermal, "thermal_buoyancy");
            settings.density_dissipation = number(dissipation, "density_dissipation");
            settings.camera_yaw = number(yaw, "camera_yaw");
            settings.camera_pitch = number(pitch, "camera_pitch");
            settings.camera_zoom = number(zoom, "camera_zoom");
            settings.pressure_iterations = iterations(pressure_budget);
            new (self) application::Settings(std::move(settings));
        }, nb::kw_only(), nb::arg("domain_box_opacity") = application::default_settings().domain_box_opacity,
            nb::arg("sources") = nb::none(), nb::arg("preset") = "smoke", nb::arg("thermal_buoyancy") = true,
            nb::arg("density_dissipation") = 0.2f, nb::arg("camera_yaw") = 0.0f, nb::arg("camera_pitch") = -0.35f,
            nb::arg("camera_zoom") = 1.0f, nb::arg("pressure_iterations") = 64)
        .def_prop_rw("domain_box_opacity", [](const application::Settings& s) { return s.domain_box_opacity; },
            [](application::Settings& s, nb::object v) { s.domain_box_opacity = number(v, "domain_box_opacity"); })
        .def_prop_rw("sources", [](const application::Settings& s) { return source_list(s); },
            [](application::Settings& s, nb::object v) { s.sources = sources(v); },
            "Returns a copied list; assign a replacement list to edit sources.")
        .def_prop_rw("preset", [](const application::Settings& s) { return s.preset; },
            [](application::Settings& s, nb::object v) { s.preset = preset(v); })
        .def_prop_rw("thermal_buoyancy", [](const application::Settings& s) { return s.thermal_buoyancy; },
            [](application::Settings& s, nb::object v) { s.thermal_buoyancy = boolean(v, "thermal_buoyancy"); })
        .def_prop_rw("density_dissipation", [](const application::Settings& s) { return s.density_dissipation; },
            [](application::Settings& s, nb::object v) { s.density_dissipation = number(v, "density_dissipation"); })
        .def_prop_rw("camera_yaw", [](const application::Settings& s) { return s.camera_yaw; },
            [](application::Settings& s, nb::object v) { s.camera_yaw = number(v, "camera_yaw"); })
        .def_prop_rw("camera_pitch", [](const application::Settings& s) { return s.camera_pitch; },
            [](application::Settings& s, nb::object v) { s.camera_pitch = number(v, "camera_pitch"); })
        .def_prop_rw("camera_zoom", [](const application::Settings& s) { return s.camera_zoom; },
            [](application::Settings& s, nb::object v) { s.camera_zoom = number(v, "camera_zoom"); })
        .def_prop_rw("pressure_iterations", [](const application::Settings& s) { return s.pressure_iterations; },
            [](application::Settings& s, nb::object v) { s.pressure_iterations = iterations(v); })
        .def("__repr__", [](const application::Settings& s) {
            return nb::str("Settings(domain_box_opacity={:g}, sources={}, preset={!r}, thermal_buoyancy={}, "
                "density_dissipation={:g}, camera_yaw={:g}, camera_pitch={:g}, camera_zoom={:g}, pressure_iterations={})").attr("format")(
                s.domain_box_opacity, source_list(s), s.preset, s.thermal_buoyancy,
                s.density_dissipation, s.camera_yaw, s.camera_pitch, s.camera_zoom, s.pressure_iterations);
        });
    m.def("default_settings", [] { return application::default_settings(); }, "Return fresh simulator defaults.");
    m.def("run", [](const application::Settings& settings) {
        auto threading = nb::module_::import_("threading");
        if (!threading.attr("current_thread")().is(threading.attr("main_thread")()))
            throw std::runtime_error("run must be called from the Python main thread");
        const auto snapshot = settings;
        nb::gil_scoped_release release;
        application::run(snapshot, check_signals);
    }, nb::arg("settings"), "Open a simulation window and block until it closes. Invalid settings raise ValueError.");
}
