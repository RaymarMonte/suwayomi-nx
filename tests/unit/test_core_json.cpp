#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "core/Json.hpp"

using snx::core::ErrorKind;
using snx::core::JsonLimits;
using snx::core::parseJson;
namespace json = snx::core::json;

namespace {

std::string nested(int depth) {
    return std::string(static_cast<std::size_t>(depth), '[') + std::string(static_cast<std::size_t>(depth), ']');
}

nlohmann::json obj(const char* text) {
    return nlohmann::json::parse(text);
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("parseJson reads valid JSON and refuses everything else as a value") {
        auto ok = parseJson(R"({"a": [1, 2, {"b": "c"}]})");
        REQUIRE(ok.ok());
        CHECK(ok.value()["a"][2]["b"] == "c");

        for (const char* bad : {"", "   ", "{", "{\"a\": }", "[1, 2,]", "nul", "{\"a\": 1} trailing", "\"\xff\xfe\""}) {
            CAPTURE(bad);
            auto r = parseJson(bad);
            REQUIRE_FALSE(r.ok());
            CHECK(r.error().kind == ErrorKind::Parse);
            CHECK_FALSE(r.error().message.empty());
        }
    }

    TEST_CASE("parseJson enforces the size limit before parsing") {
        JsonLimits limits;
        limits.maxBytes = 10;
        CHECK(parseJson("[1,2,3,4]", limits).ok());  // 9 bytes
        CHECK(parseJson("[1,2,3,45]", limits).ok());  // 10 bytes
        auto big = parseJson("[1,2,3,456]", limits);
        REQUIRE_FALSE(big.ok());
        CHECK(big.error().message.find("too big") != std::string::npos);
        CHECK(JsonLimits{}.maxBytes == 8u * 1024 * 1024);  // SAFETY.md S6
    }

    TEST_CASE("parseJson enforces the nesting limit, counting only brackets outside strings") {
        JsonLimits limits;
        CHECK(limits.maxDepth == 64);
        CHECK(parseJson(nested(64)).ok());
        auto deep = parseJson(nested(65));
        REQUIRE_FALSE(deep.ok());
        CHECK(deep.error().message.find("nested too deeply") != std::string::npos);
        // A deep bomb never reaches the parser, however long.
        CHECK_FALSE(parseJson(nested(100000)).ok());
        // Brackets inside strings, and escaped quotes, don't count.
        const std::string inString = "{\"a\": \"" + nested(200) + "\\\"" + nested(10) + "\"}";
        CHECK(parseJson(inString).ok());
        // Objects count like arrays.
        std::string objects;
        for (int i = 0; i < 65; ++i) {
            objects += "{\"k\":";
        }
        objects += "1" + std::string(65, '}');
        CHECK_FALSE(parseJson(objects).ok());
        limits.maxDepth = 2;
        CHECK(parseJson("[[1]]", limits).ok());
        CHECK_FALSE(parseJson("[[[1]]]", limits).ok());
    }

    TEST_CASE("dumpJson never throws on invalid UTF-8") {
        nlohmann::json value = {{"name", std::string("bad \xff\xfe bytes")}};
        std::string text;
        CHECK_NOTHROW(text = snx::core::dumpJson(value));
        CHECK(text.find("bad") != std::string::npos);
        CHECK(snx::core::dumpJson(nlohmann::json::array({1, 2}), 2).find('\n') != std::string::npos);
    }

    TEST_CASE("field readers: missing, null and wrong types give nothing") {
        const auto o = obj(R"({"s": "x", "n": null, "b": true, "i": 7, "f": 2.5, "a": [1], "o": {}})");
        CHECK(json::field(o, "missing") == nullptr);
        CHECK(json::field(o, "n") == nullptr);
        CHECK(json::field(nlohmann::json::array(), "s") == nullptr);
        CHECK(json::field(nlohmann::json(), "s") == nullptr);
        CHECK(json::object(o, "o") != nullptr);
        CHECK(json::object(o, "a") == nullptr);
        CHECK(json::array(o, "a") != nullptr);
        CHECK(json::array(o, "o") == nullptr);
        CHECK(json::string(o, "s") == "x");
        CHECK_FALSE(json::string(o, "i").has_value());
        CHECK(json::boolean(o, "b") == true);
        CHECK_FALSE(json::boolean(o, "s").has_value());
        CHECK_FALSE(json::boolean(o, "i").has_value());
        CHECK(json::number(o, "f") == 2.5);
        CHECK(json::number(o, "i") == 7.0);
        CHECK_FALSE(json::number(o, "s").has_value());
        CHECK_FALSE(json::number(nlohmann::json{{"x", std::nan("")}}, "x").has_value());
        CHECK_FALSE(json::number(nlohmann::json{{"x", std::numeric_limits<double>::infinity()}}, "x").has_value());
    }

    TEST_CASE("int32 accepts whole numbers that fit in 32 bits") {
        const auto o = obj(R"({"a": 5, "b": -2147483648, "c": 2147483647, "d": 2147483648, "e": -2147483649, "f": 3.0,
                              "g": 3.5, "h": "5", "i": true, "j": 18446744073709551615, "k": 1e300, "l": -0.0})");
        CHECK(json::int32(o, "a") == 5);
        CHECK(json::int32(o, "b") == std::numeric_limits<int>::min());
        CHECK(json::int32(o, "c") == std::numeric_limits<int>::max());
        CHECK_FALSE(json::int32(o, "d").has_value());
        CHECK_FALSE(json::int32(o, "e").has_value());
        CHECK(json::int32(o, "f") == 3);
        CHECK_FALSE(json::int32(o, "g").has_value());
        CHECK_FALSE(json::int32(o, "h").has_value());
        CHECK_FALSE(json::int32(o, "i").has_value());
        CHECK_FALSE(json::int32(o, "j").has_value());
        CHECK_FALSE(json::int32(o, "k").has_value());
        CHECK(json::int32(o, "l") == 0);
    }

    TEST_CASE("int64 reads LongString text and whole numbers that fit in 64 bits") {
        const auto o = obj(R"({"ms": "1767312180000", "neg": "-5", "max": "9223372036854775807", "over": "9223372036854775808",
                              "min": "-9223372036854775808", "under": "-9223372036854775809", "long": "00000000000000000001",
                              "junk": "12a", "empty": "", "dash": "-", "plus": "+5", "space": " 5", "num": 1767312180000,
                              "u": 18446744073709551615, "fl": 1e30, "half": 2.5, "whole": 4.0, "neg2": -7})");
        CHECK(json::int64(o, "ms") == 1767312180000LL);
        CHECK(json::int64(o, "neg") == -5);
        CHECK(json::int64(o, "max") == std::numeric_limits<std::int64_t>::max());
        CHECK_FALSE(json::int64(o, "over").has_value());
        CHECK(json::int64(o, "min") == std::numeric_limits<std::int64_t>::min());
        CHECK_FALSE(json::int64(o, "under").has_value());
        CHECK_FALSE(json::int64(o, "long").has_value());
        for (const char* key : {"junk", "empty", "dash", "plus", "space", "u", "fl", "half"}) {
            CAPTURE(key);
            CHECK_FALSE(json::int64(o, key).has_value());
        }
        CHECK(json::int64(o, "num") == 1767312180000LL);
        CHECK(json::int64(o, "whole") == 4);
        CHECK(json::int64(o, "neg2") == -7);
        CHECK_FALSE(json::asInt64(nlohmann::json(true)).has_value());
        CHECK_FALSE(json::asInt64(nlohmann::json(9223372036854775808.0)).has_value());
        CHECK(json::asInt64(nlohmann::json(-9223372036854775808.0)) == std::numeric_limits<std::int64_t>::min());
    }

    TEST_CASE("strings keeps only the text entries of a list") {
        const auto o = obj(R"({"g": ["Action", 5, null, "Test", ["x"]], "s": "Action"})");
        CHECK(json::strings(o, "g") == std::vector<std::string>{"Action", "Test"});
        CHECK(json::strings(o, "s").empty());
        CHECK(json::strings(o, "missing").empty());
    }
}
