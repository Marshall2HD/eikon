#pragma once

// The Snake game: an 18 × 8 board of 3 pt cells drawn over the
// toolbar logo. Columns run left to right, rows bottom to top (y up).

#include <cstdint>
#include <deque>
#include <functional>
#include <string_view>
#include <vector>

namespace Eikon::Core
{
    class SnakeGame
    {
    public:
        static constexpr int kCols = 18;
        static constexpr int kRows = 8;
        static constexpr double kCellSize = 3.0;
        static constexpr double kGap = 1.0;
        static constexpr int kFirstTickMs = 400;  // The first step after 0.4 s
        static constexpr int kStartDelayMs = 200;
        static constexpr int kScoreMs = 1500;     // the score stays up 1.5 s after the game ends
        // The field layer: (cell + gap) × count − gap, plus 1 on each side (73 × 33).
        static constexpr double FieldWidth() { return (kCellSize + kGap) * kCols - kGap + 2; }
        static constexpr double FieldHeight() { return (kCellSize + kGap) * kRows - kGap + 2; }

        enum class Direction { Up, Down, Left, Right };
        struct Cell
        {
            int col = 0;
            int row = 0;
            bool operator==(Cell const&) const = default;
        };

        // Uniform integer in [0, bound).
        using Random = std::function<std::uint64_t(std::uint64_t bound)>;
        explicit SnakeGame(Random random);

        // A fresh snake and bait; on (field shown, logo hidden) and playing.
        void Start();
        // One step. Returns the delay in ms before the next step, or -1 when the game
        // has ended and the logo comes back (isOn = false).
        int Tick();
        // An arrow key. A turn onto the current axis (checked against the last queued
        // turn, else the current direction) is ignored; others are queued, one per step.
        void Steer(Direction direction);

        bool IsOn() const noexcept { return m_isOn; }
        bool Playing() const noexcept { return m_playing; }
        int Score() const noexcept { return m_score; }
        int Delay() const noexcept { return m_delay; }
        Cell Bait() const noexcept { return m_bait; }
        std::vector<Cell> const& Body() const noexcept { return m_snake; }   // tail first, head last
        bool Lit(int col, int row) const noexcept { return m_matrix[col][row]; }

        // An unlit cell's shade by its distance to the border (0 at the edge, 3 = inner).
        static int Band(int col, int row) noexcept;
        // Shade alpha for a band: black 0.06/0.05/0.04/0.03 light, white 0.10/0.09/0.08/0.07 dark.
        static double BandAlpha(int band, bool dark) noexcept;

        // A run-length pattern ("2 4 3 …": alternating unlit/lit runs) laid down column
        // by column from `startCol`, each column top to bottom.
        void DrawPattern(std::string_view runs, int startCol);
        // The score as one or two digits.
        void DrawScore(int score);

        // For tests: put the game in a known position.
        void SetForTest(std::vector<Cell> snake, Cell bait, Direction direction);

    private:
        void Reset();
        void Redraw();
        void PlaceBait();             // loops
        bool Collides(Cell cell) const noexcept;
        void Clear() noexcept;

        Random m_random;
        bool m_matrix[kCols][kRows]{};
        std::vector<Cell> m_snake;
        Cell m_bait;
        Direction m_direction = Direction::Right;
        std::deque<Direction> m_queue;
        int m_delay = 0;
        int m_score = 0;
        bool m_playing = false;
        bool m_isOn = false;
    };
}
