// Verifies: REQ-COM-001, REQ-DEV-003
//
// The command line of the hand-run check (ADR-0021 follow-up 3), as a pure function
// of its arguments so that the default suite can test it without opening anything.
// The tool is run by a person on real hardware, so a mistyped argument must be
// refused with a message and never guessed at.

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "pigeon/raspberry/link_check_arguments.hpp"

namespace {

using pigeon::raspberry::parse_link_check_arguments;

TEST(LinkCheckArguments, OnlyThePortIsRequiredAndTheRestHaveDefaults) {
  const auto parsed = parse_link_check_arguments({"--port", "/the/port"});

  ASSERT_TRUE(parsed.ok()) << parsed.error;
  EXPECT_EQ(parsed.port, "/the/port");
  EXPECT_EQ(parsed.baud_rate, 115200U);
  EXPECT_EQ(parsed.max_wait_seconds, 10);
  EXPECT_EQ(parsed.options.safe_state_exchanges, 3);
  EXPECT_FALSE(parsed.options.aim.has_value());  // nothing moves unless asked
}

TEST(LinkCheckArguments, EveryOptionCanBeGiven) {
  const auto parsed =
      parse_link_check_arguments({"--port", "/p", "--baud", "57600", "--wait-seconds", "30",
                                  "--exchanges", "7", "--aim", "-12.5", "20"});

  ASSERT_TRUE(parsed.ok()) << parsed.error;
  EXPECT_EQ(parsed.baud_rate, 57600U);
  EXPECT_EQ(parsed.max_wait_seconds, 30);
  EXPECT_EQ(parsed.options.safe_state_exchanges, 7);
  ASSERT_TRUE(parsed.options.aim.has_value());
  EXPECT_DOUBLE_EQ(parsed.options.aim->x.degrees, -12.5);
  EXPECT_DOUBLE_EQ(parsed.options.aim->y.degrees, 20.0);
}

TEST(LinkCheckArguments, HelpIsAskedForAndIsNotAnError) {
  const auto parsed = parse_link_check_arguments({"--help"});

  EXPECT_TRUE(parsed.help);
  EXPECT_FALSE(parsed.ok());  // there is nothing to run
  EXPECT_TRUE(parsed.error.empty());
  EXPECT_NE(pigeon::raspberry::link_check_usage().find("--port"), std::string_view::npos);
}

TEST(LinkCheckArguments, TheUsageSaysThatItNeverFiresAndThatAimMovesTheServos) {
  const std::string usage{pigeon::raspberry::link_check_usage()};

  EXPECT_NE(usage.find("never fires"), std::string::npos) << usage;
  EXPECT_NE(usage.find("moves the servos"), std::string::npos) << usage;
}

TEST(LinkCheckArguments, NoPortIsRefused) {
  const auto parsed = parse_link_check_arguments({});

  EXPECT_FALSE(parsed.ok());
  EXPECT_NE(parsed.error.find("--port"), std::string::npos) << parsed.error;
}

TEST(LinkCheckArguments, AnUnknownOptionIsRefusedByName) {
  const auto parsed = parse_link_check_arguments({"--port", "/p", "--fire", "300"});

  EXPECT_FALSE(parsed.ok());
  EXPECT_NE(parsed.error.find("--fire"), std::string::npos) << parsed.error;
}

TEST(LinkCheckArguments, AnOptionWithNoValueIsRefused) {
  for (const char* option : {"--port", "--baud", "--wait-seconds", "--exchanges"}) {
    // The option is last, so there is nothing after it to be its value.
    const auto parsed = parse_link_check_arguments({"--port", "/p", option});
    EXPECT_FALSE(parsed.ok()) << option;
  }
}

TEST(LinkCheckArguments, AimNeedsTwoNumbers) {
  EXPECT_FALSE(parse_link_check_arguments({"--port", "/p", "--aim"}).ok());
  EXPECT_FALSE(parse_link_check_arguments({"--port", "/p", "--aim", "10"}).ok());
  EXPECT_FALSE(parse_link_check_arguments({"--port", "/p", "--aim", "10", "west"}).ok());
}

TEST(LinkCheckArguments, ANumberThatIsNotOneIsRefusedNotGuessedAt) {
  for (const std::vector<std::string>& bad : {
           std::vector<std::string>{"--port", "/p", "--baud", "fast"},
           std::vector<std::string>{"--port", "/p", "--baud", "-9600"},
           std::vector<std::string>{"--port", "/p", "--wait-seconds", "ten"},
           std::vector<std::string>{"--port", "/p", "--wait-seconds", "0"},
           std::vector<std::string>{"--port", "/p", "--exchanges", "-1"},
           std::vector<std::string>{"--port", "/p", "--exchanges", "3x"},
           std::vector<std::string>{"--port", "/p", "--aim", "nan", "10"},
           std::vector<std::string>{"--port", "/p", "--aim", "10", "inf"},
       }) {
    EXPECT_FALSE(parse_link_check_arguments(bad).ok()) << bad.back();
  }
}

TEST(LinkCheckArguments, ZeroExchangesIsAllowedSoThatOnlyStartUpIsChecked) {
  const auto parsed = parse_link_check_arguments({"--port", "/p", "--exchanges", "0"});

  ASSERT_TRUE(parsed.ok()) << parsed.error;
  EXPECT_EQ(parsed.options.safe_state_exchanges, 0);
}

}  // namespace
