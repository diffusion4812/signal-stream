#include <chrono>
#include <thread>
#include <cstdarg>

#include <gtest/gtest.h>
#include "projectenvironment.h"

ProjectManager mgr;
ProjectData pdata;
Schema schema;
std::string err;

enum class Color { RED, GREEN, YELLOW, RESET };

inline const char* ColorCode(Color c) {
    switch (c) {
    case Color::RED: return "\x1B[31m";
    case Color::GREEN: return "\x1B[32m";
    case Color::YELLOW: return "\x1B[33m";
    default: return "\x1B[0m";
    }
}

inline void ColoredPrint(Color c, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::cout << ColorCode(c);
    // Simple use of vprintf to stdout (or format into std::string with vsnprintf)
    vprintf(fmt, args);
    std::cout << ColorCode(Color::RESET);
    va_end(args);
}

TEST(SignalStream, OpenProject) {
    LoadProjectFromFile("C:/Users/LOAR02/Downloads/test_project.json", pdata, err);
    mgr.LoadProject(pdata, false, err); // Load and start services

    EXPECT_EQ(mgr.GetProjectData().name, "Test Project");
    EXPECT_EQ(mgr.GetProjectData().sources.size(), 1);
}

// This works because LoadProject has already finalised the schemas
TEST(SignalStream, StartServices) {
    bool started;
    started = mgr.StartAllServices(err);
    EXPECT_EQ(started, true);
}

// Fail because you cannot update the schema when a service is running
TEST(SignalStream, AddSchema_FAIL1) {
    bool schemaloaded = false;
    schema.add_field("field1", Kind::Int32);
    schema.add_field("field2", Kind::String);
    auto svc = mgr.GetService("my random data");
    if (svc) {
        schemaloaded = svc->SetupSchema(schema);
    }
    EXPECT_FALSE(schemaloaded);
}

// Fail because you cannot update the schema if it is not finalised
TEST(SignalStream, AddSchema_FAIL2) {
    bool schemaloaded = false;
    auto svc = mgr.GetService("my random data");
    if (svc) {
        svc->Stop();
        schemaloaded = svc->SetupSchema(schema);
    }
    EXPECT_FALSE(schemaloaded);
}

TEST(SignalStream, AddSchema_PASS) {
    bool schemaloaded = false;
    schema.finalise(); // Must finalise before use
    auto svc = mgr.GetService("my random data");
    if (svc) {
        schemaloaded = svc->SetupSchema(schema);
    }
    EXPECT_TRUE(schemaloaded);
}

TEST(SignalStream, GetRandomData) {
    auto svc = mgr.GetService("my random data");
    Instance sampleinstance(schema);
    SampleHandle samplehandle;
    svc->TryAcquireSample(samplehandle, sampleinstance);

    std::optional<int32_t> myval = sampleinstance.get<int32_t>("field1");
    ASSERT_TRUE(myval.has_value());
    ColoredPrint(Color::YELLOW, "             myval = %d\n", myval);
    EXPECT_EQ(myval.value(), 1357924680);
}