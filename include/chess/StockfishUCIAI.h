#pragma once

// Stockfish adapter: speaks UCI to a Stockfish binary over a subprocess pipe
// using SDL3's process API. Stockfish is deliberately NOT linked into the
// binary (it is GPLv3) - it runs as an external process, so this project's
// own source stays MIT. See docs/adapters.md and docs/strength.md.
//
// Only used for: best move selection and position evaluation. All board
// legality lives in ChessLibBoard; Stockfish never mutates game state.

#include "chess/IChessAI.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <SDL3/SDL_process.h>

namespace wchess
{
	class StockfishUCIAI : public IChessAI
	{
	public:
		explicit StockfishUCIAI(const std::string& binaryPath);
		~StockfishUCIAI() override;

		// Not copyable/movable: owns a process handle.
		StockfishUCIAI(const StockfishUCIAI&) = delete;
		StockfishUCIAI& operator=(const StockfishUCIAI&) = delete;

		bool isAvailable() const override;
		std::string name() const override;

		void setStrength(int skill, int elo) override;
		void setPosition(const std::string& fen) override;
		Move bestMove(const std::vector<Move>& legalMoves) override;
		Eval evaluate(const std::string& fen, int movetimeMs) override;
		void shutdown() override;

	private:
		// Sends a raw line + '\n' and flushes. Returns false on I/O error.
		bool sendLine(const std::string& line);

		// Reads output lines until one starts with any of `until` tokens
		// (prefix match on the first word). Collects "info" lines for parsing.
		// Returns the terminating line.
		std::string readUntil(const std::vector<std::string>& until);

		// Extracts "score cp X" / "score mate Y" and "pv ..." from info lines.
		static Eval parseInfoLines(const std::vector<std::string>& infoLines);

		// "setoption name X value Y" + isready/readyok round-trip.
		bool setOption(const std::string& name, const std::string& value);

		bool init(); // uci handshake + options

		std::string m_binaryPath;
		SDL_Process* m_process = nullptr;
		SDL_IOStream* m_input = nullptr;
		SDL_IOStream* m_output = nullptr;

		std::atomic<bool> m_available{false};
		std::atomic<bool> m_shutdown{false};

		int m_skill = 12;
		int m_elo = 1700;
		std::string m_fen; // last position sent
	};
} // namespace wchess
