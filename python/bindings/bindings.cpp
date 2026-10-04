#include <nanobind/nanobind.h>
#include "application/application.hpp"
#include <cmath>
#include <limits>
#include <string>
#include <stdexcept>

namespace nb = nanobind;
namespace {
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
        .def("__init__", [](application::Settings* self, nb::object opacity, nb::object input_sources) {
            auto settings = application::default_settings();
            settings.domain_box_opacity = number(opacity, "domain_box_opacity");
            settings.sources = sources(input_sources);
            new (self) application::Settings(std::move(settings));
        }, nb::kw_only(), nb::arg("domain_box_opacity") = application::default_settings().domain_box_opacity, nb::arg("sources") = nb::none())
        .def_prop_rw("domain_box_opacity", [](const application::Settings& s) { return s.domain_box_opacity; },
            [](application::Settings& s, nb::object v) { s.domain_box_opacity = number(v, "domain_box_opacity"); })
        .def_prop_rw("sources", [](const application::Settings& s) { return source_list(s); },
            [](application::Settings& s, nb::object v) { s.sources = sources(v); },
            "Returns a copied list; assign a replacement list to edit sources.")
        .def("__repr__", [](const application::Settings& s) {
            return nb::str("Settings(domain_box_opacity={:g}, sources={})").attr("format")(
                s.domain_box_opacity, source_list(s));
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
