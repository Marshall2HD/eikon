#include "pch.h"
#include "TestFramework.h"

#include "Core/Snake.h"

using Eikon::Core::SnakeGame;
using Cell = SnakeGame::Cell;
using Direction = SnakeGame::Direction;

namespace
{
    // Replays fixed values (each taken modulo the bound), then zeros.
    SnakeGame::Random Sequence(std::vector<std::uint64_t> values)
    {
        auto index = std::make_shared<std::size_t>(0);
        return [values = std::move(values), index](std::uint64_t bound) {
            std::uint64_t const value = *index < values.size() ? values[(*index)++] : 0;
            return value % bound;
        };
    }
}

TEST(Snake_StartsWithFourCellsHeadingRightAndBaitOffCorners)
{
    // Bait candidates: (0,0) corner, (5,3) on the snake, (17,7) corner, then (10,2).
    SnakeGame game{ Sequence({ 0, 0, 5, 3, 17, 7, 10, 2 }) };
    game.Start();
    CHECK(game.IsOn());
    CHECK(game.Playing());
    CHECK_EQ(game.Delay(), 200);
    CHECK_EQ(game.Body().size(), std::size_t{ 4 });
    CHECK(game.Body().front() == (Cell{ 4, 3 }));
    CHECK(game.Body().back() == (Cell{ 7, 3 }));
    CHECK(game.Bait() == (Cell{ 10, 2 }));
    CHECK(game.Lit(10, 2));
    CHECK(game.Lit(4, 3));
    CHECK(!game.Lit(3, 3));
    CHECK_EQ(SnakeGame::FieldWidth(), 73.0);
    CHECK_EQ(SnakeGame::FieldHeight(), 33.0);
}

TEST(Snake_MovesDropsTheTailAndEatsToGrowFaster)
{
    SnakeGame game{ Sequence({ 2, 5 }) };
    game.SetForTest({ { 4, 3 }, { 5, 3 }, { 6, 3 }, { 7, 3 } }, { 9, 3 }, Direction::Right);
    CHECK_EQ(game.Tick(), 200);
    CHECK(game.Body().front() == (Cell{ 5, 3 }));
    CHECK(game.Body().back() == (Cell{ 8, 3 }));
    CHECK(!game.Lit(4, 3));
    // Onto the bait: one cell longer, score 1, delay trunc(200 × 0.96) = 192, new bait (2, 5).
    CHECK_EQ(game.Tick(), 192);
    CHECK_EQ(game.Body().size(), std::size_t{ 5 });
    CHECK(game.Body().front() == (Cell{ 5, 3 }));
    CHECK(game.Body().back() == (Cell{ 9, 3 }));
    CHECK_EQ(game.Score(), 1);
    CHECK(game.Bait() == (Cell{ 2, 5 }));
}

TEST(Snake_TurnsOntoTheSameAxisAreIgnoredAgainstTheLastQueuedTurn)
{
    SnakeGame game{ Sequence({}) };
    game.SetForTest({ { 4, 3 }, { 5, 3 } }, { 0, 7 }, Direction::Right);
    game.Steer(Direction::Left);    // same axis as Right: ignored
    game.Steer(Direction::Up);      // queued
    game.Steer(Direction::Down);    // same axis as the queued Up: ignored
    game.Steer(Direction::Left);    // queued after Up
    game.Tick();
    CHECK(game.Body().back() == (Cell{ 5, 4 }));   // up = row + 1 (y-up board)
    game.Tick();
    CHECK(game.Body().back() == (Cell{ 4, 4 }));
    game.Tick();
    CHECK(game.Body().back() == (Cell{ 3, 4 }));   // no more turns queued
}

TEST(Snake_WallEndsTheGameShowsTheScoreThenTheLogoReturns)
{
    SnakeGame game{ Sequence({}) };
    game.SetForTest({ { 16, 3 }, { 17, 3 } }, { 0, 7 }, Direction::Right);
    CHECK_EQ(game.Tick(), 1500);
    CHECK(!game.Playing());
    CHECK(game.IsOn());
    // Score 0: digit 0 at column 7 (its left column lit on rows 2-5).
    CHECK(game.Lit(7, 5));
    CHECK(game.Lit(7, 2));
    CHECK(!game.Lit(7, 6));
    CHECK(!game.Lit(17, 3));
    CHECK_EQ(game.Tick(), -1);
    CHECK(!game.IsOn());
}

TEST(Snake_RunningIntoTheTailCellIsACollision)
{
    // The new head is checked against every segment before the tail moves.
    SnakeGame game{ Sequence({}) };
    game.SetForTest({ { 5, 4 }, { 5, 3 }, { 6, 3 }, { 6, 4 } }, { 0, 7 }, Direction::Left);
    CHECK_EQ(game.Tick(), 1500);
    CHECK(!game.Playing());
}

TEST(Snake_ScoreDigitsUseRunLengthPatterns)
{
    SnakeGame game{ Sequence({}) };
    // "17 6 121" at column 7: a bar in column 9, rows 1-6 counted from the top.
    game.DrawScore(1);
    for (int row = 1; row <= 6; ++row)
    {
        CHECK(game.Lit(9, row));
    }
    CHECK(!game.Lit(9, 7));
    CHECK(!game.Lit(9, 0));
    CHECK(!game.Lit(8, 4));
    // 10: the 1 at column 4 (bar in column 6), the 0 at column 9 (columns 9-12).
    game.DrawScore(10);
    CHECK(game.Lit(6, 6));
    CHECK(game.Lit(6, 1));
    CHECK(!game.Lit(9, 1));
    CHECK(game.Lit(9, 2));
    CHECK(game.Lit(10, 6));
    CHECK(game.Lit(10, 1));
    CHECK(!game.Lit(10, 4));
    CHECK(game.Lit(12, 5));
    CHECK(!game.Lit(13, 5));
}

TEST(Snake_CellShadeDarkensTowardsTheBorder)
{
    CHECK_EQ(SnakeGame::Band(0, 4), 3);
    CHECK_EQ(SnakeGame::Band(17, 7), 3);
    CHECK_EQ(SnakeGame::Band(1, 4), 2);
    CHECK_EQ(SnakeGame::Band(9, 2), 1);
    CHECK_EQ(SnakeGame::Band(9, 4), 0);
    CHECK_EQ(SnakeGame::BandAlpha(3, false), 0.06);
    CHECK_EQ(SnakeGame::BandAlpha(0, false), 0.03);
    CHECK_EQ(SnakeGame::BandAlpha(3, true), 0.10);
    CHECK_EQ(SnakeGame::BandAlpha(0, true), 0.07);
}
