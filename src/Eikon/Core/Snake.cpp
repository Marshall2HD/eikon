#include "pch.h"
#include "Core/Snake.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>

namespace Eikon::Core
{
    namespace
    {
        // The digit patterns (run lengths, unlit first, column by column from the
        // pattern's first column, each column top to bottom).
        constexpr std::array<std::string_view, 10> kDigits{
            "2 4 3 1 4 1 2 1 4 1 3 4 114",
            "17 6 121",
            "2 1 3 1 2 1 3 2 2 1 2 1 1 1 3 2 2 1 113",
            "2 1 2 1 3 1 4 1 2 1 1 1 2 1 3 1 1 2 114",
            "1 4 7 1 7 1 4 6 113",
            "1 3 2 1 2 1 1 1 2 1 2 1 1 1 2 1 2 1 2 2 114",
            "2 4 3 1 1 1 2 1 2 1 1 1 2 1 5 2 114",
            "1 1 7 1 7 1 7 6 113",
            "1 6 2 1 1 1 2 1 2 1 1 1 2 1 2 6 113",
            "2 2 5 1 2 1 1 1 2 1 2 1 1 1 3 4 114",
        };

        bool SameAxis(SnakeGame::Direction a, SnakeGame::Direction b) noexcept
        {
            bool const horizontalA = a == SnakeGame::Direction::Left || a == SnakeGame::Direction::Right;
            bool const horizontalB = b == SnakeGame::Direction::Left || b == SnakeGame::Direction::Right;
            return horizontalA == horizontalB;
        }

        bool IsCorner(SnakeGame::Cell cell) noexcept
        {
            bool const edgeCol = cell.col == 0 || cell.col == SnakeGame::kCols - 1;
            bool const edgeRow = cell.row == 0 || cell.row == SnakeGame::kRows - 1;
            return edgeCol && edgeRow;
        }
    }

    SnakeGame::SnakeGame(Random random)
        : m_random(std::move(random))
    {
        Reset();
    }

    void SnakeGame::Reset()
    {
        // The static snake at: four cells on row 3, heading right.
        m_snake = { { 4, 3 }, { 5, 3 }, { 6, 3 }, { 7, 3 } };
        m_delay = kStartDelayMs;
        m_score = 0;
        m_direction = Direction::Right;
        m_queue.clear();
    }

    void SnakeGame::Clear() noexcept
    {
        for (auto& column : m_matrix)
        {
            std::fill(std::begin(column), std::end(column), false);
        }
    }

    void SnakeGame::Redraw()
    {
        Clear();
        for (Cell const& cell : m_snake)
        {
            if (cell.col >= 0 && cell.col < kCols && cell.row >= 0 && cell.row < kRows)
            {
                m_matrix[cell.col][cell.row] = true;
            }
        }
        m_matrix[m_bait.col][m_bait.row] = true;
    }

    bool SnakeGame::Collides(Cell cell) const noexcept
    {
        if (cell.col < 0 || cell.col >= kCols || cell.row < 0 || cell.row >= kRows)
        {
            return true;
        }
        // Every segment counts, the tail included (it has not moved yet).
        return std::find(m_snake.begin(), m_snake.end(), cell) != m_snake.end();
    }

    void SnakeGame::PlaceBait()
    {
        // Random cells until one is off the snake and not a corner; on a full board it gives up after many
        // tries and keeps the last cell.
        for (int attempt = 0; attempt < 100000; ++attempt)
        {
            Cell const cell{ static_cast<int>(m_random(kCols)), static_cast<int>(m_random(kRows)) };
            m_bait = cell;
            if (!IsCorner(cell) && std::find(m_snake.begin(), m_snake.end(), cell) == m_snake.end())
            {
                return;
            }
        }
    }

    void SnakeGame::Start()
    {
        Reset();
        PlaceBait();
        Redraw();
        m_playing = true;
        m_isOn = true;
    }

    void SnakeGame::Steer(Direction direction)
    {
        Direction const last = m_queue.empty() ? m_direction : m_queue.back();
        if (SameAxis(last, direction))
        {
            return;
        }
        m_queue.push_back(direction);
    }

    int SnakeGame::Tick()
    {
        if (!m_playing)
        {
            m_isOn = false;
            return -1;
        }
        if (!m_queue.empty())
        {
            m_direction = m_queue.front();
            m_queue.pop_front();
        }
        Cell head = m_snake.back();
        switch (m_direction)
        {
        case Direction::Up: ++head.row; break;
        case Direction::Down: --head.row; break;
        case Direction::Right: ++head.col; break;
        case Direction::Left: --head.col; break;
        }
        if (Collides(head))
        {
            // Game over: the score in digits. The board is reset behind the digits and the logo returns after 1.5 s.
            DrawScore(m_score);
            int const score = m_score;
            Reset();
            m_score = score;
            m_playing = false;
            return kScoreMs;
        }
        if (head == m_bait)
        {
            // Grow onto the bait, score, 4 % faster, new bait.
            m_snake.push_back(head);
            ++m_score;
            m_delay = static_cast<int>(std::trunc(m_delay * 0.96));
            PlaceBait();
        }
        else
        {
            // Every segment takes the next one's place, the head the new cell.
            std::rotate(m_snake.begin(), m_snake.begin() + 1, m_snake.end());
            m_snake.back() = head;
        }
        Redraw();
        return m_delay;
    }

    int SnakeGame::Band(int col, int row) noexcept
    {
        int const distance = (std::min)({ col, row, kCols - 1 - col, kRows - 1 - row });
        return 3 - (std::min)(distance, 3);
    }

    double SnakeGame::BandAlpha(int band, bool dark) noexcept
    {
        static constexpr double kLight[4]{ 0.03, 0.04, 0.05, 0.06 };
        static constexpr double kDark[4]{ 0.07, 0.08, 0.09, 0.10 };
        int const i = std::clamp(band, 0, 3);
        return dark ? kDark[i] : kLight[i];
    }

    void SnakeGame::DrawPattern(std::string_view runs, int startCol)
    {
        std::vector<int> counts;
        for (std::size_t i = 0; i < runs.size();)
        {
            std::size_t const end = (std::min)(runs.find(' ', i), runs.size());
            int value = 0;
            std::from_chars(runs.data() + i, runs.data() + end, value);
            counts.push_back(value);
            i = end + 1;
        }
        std::size_t next = 0;
        int remaining = counts.empty() ? 0 : counts[next++];
        bool lit = false;
        for (int col = (std::max)(startCol, 0); col < kCols; ++col)
        {
            for (int r = 0; r < kRows; ++r)
            {
                m_matrix[col][kRows - 1 - r] = lit;
                if (--remaining == 0)
                {
                    if (next >= counts.size())
                    {
                        return;
                    }
                    remaining = counts[next++];
                    lit = !lit;
                }
            }
        }
    }

    void SnakeGame::DrawScore(int score)
    {
        Clear();
        // Two digits at most.
        score = std::clamp(score, 0, 99);
        int const tens = score / 10;
        int const ones = score % 10;
        if (tens == 0)
        {
            DrawPattern(kDigits[ones], 7);
        }
        else
        {
            DrawPattern(kDigits[tens], 4);
            DrawPattern(kDigits[ones], 9);
        }
    }

    void SnakeGame::SetForTest(std::vector<Cell> snake, Cell bait, Direction direction)
    {
        m_snake = std::move(snake);
        m_bait = bait;
        m_direction = direction;
        m_queue.clear();
        m_playing = true;
        m_isOn = true;
        Redraw();
    }
}
