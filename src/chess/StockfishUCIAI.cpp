#include "chess/StockfishUCIAI.h"

#include "chess/ChessLibBoard.h"
#include "config.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

#include <SDL3/SDL.h>

namespace wchess {
namespace {
// The stdio streams of an SDL_CreateProcess are NON-BLOCKING:
// SDL_ReadIO returns 0 with status NOT_READY when no data is
// available yet. Poll with a small delay until a full line arrives
// (or timeoutMs elapses).
bool readLine(SDL_IOStream *stream, std::string &line, int timeoutMs = 60000) {
  line.clear();
  char c;
  int waited = 0;
  while (waited < timeoutMs) {
    size_t n = SDL_ReadIO(stream, &c, 1);
    if (n == 1) {
      if (c == '\n')
        return true;
      line += c;
      continue;
    }
    SDL_IOStatus status = SDL_GetIOStatus(stream);
    if (status == SDL_IO_STATUS_NOT_READY) {
      SDL_Delay(1);
      ++waited;
      continue;
    }
    return false; // EOF or error: the process is gone
  }
  return false;
}

// Writes the full payload, retrying while the pipe is full.
bool writeAll(SDL_IOStream *stream, const void *data, size_t size) {
  const char *ptr = static_cast<const char *>(data);
  size_t off = 0;
  while (off < size) {
    size_t written = SDL_WriteIO(stream, ptr + off, size - off);
    if (written == 0) {
      SDL_IOStatus status = SDL_GetIOStatus(stream);
      if (status == SDL_IO_STATUS_NOT_READY) {
        SDL_Delay(1);
        continue;
      }
      return false;
    }
    off += written;
  }
  return true;
}

bool fileExists(const std::string &path) {
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return false;
  std::fclose(f);
  return true;
}
} // namespace

StockfishUCIAI::StockfishUCIAI(const std::string &binaryPath)
    : m_binaryPath(binaryPath) {
  if (!fileExists(m_binaryPath)) {
    std::cout << "[StockfishUCIAI] binary not found at '" << m_binaryPath << "'"
              << std::endl;
    return;
  }

  const char *args[] = {m_binaryPath.c_str(), nullptr};
  m_process = SDL_CreateProcess(args, true);
  if (!m_process) {
    std::cout << "[StockfishUCIAI] failed to start process: " << SDL_GetError()
              << std::endl;
    return;
  }

  m_input = SDL_GetProcessInput(m_process);
  m_output = SDL_GetProcessOutput(m_process);
  if (!m_input || !m_output) {
    std::cout << "[StockfishUCIAI] failed to open process pipes: "
              << SDL_GetError() << std::endl;
    SDL_DestroyProcess(m_process);
    m_process = nullptr;
    return;
  }

  if (init()) {
    m_available = true;
    std::cout << "[StockfishUCIAI] ready (" << m_binaryPath << ")" << std::endl;
  } else {
    std::cout << "[StockfishUCIAI] UCI handshake failed" << std::endl;
    SDL_DestroyProcess(m_process);
    m_process = nullptr;
  }
}

StockfishUCIAI::~StockfishUCIAI() { shutdown(); }

bool StockfishUCIAI::isAvailable() const { return m_available.load(); }

std::string StockfishUCIAI::name() const { return "stockfish"; }

bool StockfishUCIAI::sendLine(const std::string &line) {
  if (!m_input || m_shutdown.load())
    return false;
  std::string out = line + "\n";
  if (!writeAll(m_input, out.data(), out.size()))
    return false;
  // The process stdio streams are unbuffered; flush is a no-op that can
  // report spurious errors on some platforms - ignore it.
  SDL_FlushIO(m_input);
  return true;
}

std::string StockfishUCIAI::readUntil(const std::vector<std::string> &until) {
  std::string line;
  int guard = 0;
  while (readLine(m_output, line, 60000)) {
    if (++guard > 500000)
      break;
    for (const auto &token : until) {
      // match the first word
      size_t space = line.find(' ');
      std::string first =
          space == std::string::npos ? line : line.substr(0, space);
      if (first == token)
        return line;
    }
  }
  // Process died or pipe closed: engine is unusable.
  m_available = false;
  return "";
}

bool StockfishUCIAI::setOption(const std::string &name,
                               const std::string &value) {
  if (!sendLine("setoption name " + name + " value " + value))
    return false;
  if (!sendLine("isready"))
    return false;
  return readUntil({"readyok"}) == "readyok";
}

bool StockfishUCIAI::init() {
  if (!sendLine("uci"))
    return false;
  std::string line = readUntil({"uciok"});
  if (line != "uciok")
    return false;

  // Human-friendly strength: cap Stockfish instead of using full
  // strength (see docs/strength.md).
  setOption("UCI_LimitStrength", "true");
  setOption("UCI_Elo", std::to_string(m_elo));
  setOption("Skill Level", std::to_string(m_skill));
  sendLine("ucinewgame");
  return sendLine("isready") && readUntil({"readyok"}) == "readyok";
}

void StockfishUCIAI::setStrength(int skill, int elo) {
  m_skill = std::clamp(skill, 0, 20);
  m_elo = std::clamp(elo, 1320, 3190);
  if (m_available.load()) {
    setOption("UCI_Elo", std::to_string(m_elo));
    setOption("Skill Level", std::to_string(m_skill));
  }
}

void StockfishUCIAI::setPosition(const std::string &fen) {
  if (!m_available.load())
    return;
  m_fen = fen;
  sendLine("position fen " + fen);
}

Eval StockfishUCIAI::parseInfoLines(const std::vector<std::string> &infoLines) {
  Eval best;
  int bestDepth = -1;

  for (const auto &line : infoLines) {
    // tokens: info depth 12 seldepth 18 multipv 1 score cp 23 nodes ... pv e2e4
    // e7e5
    std::vector<std::string> tokens;
    size_t start = 0;
    while (start <= line.size()) {
      size_t end = line.find(' ', start);
      if (end == std::string::npos) {
        tokens.push_back(line.substr(start));
        break;
      }
      tokens.push_back(line.substr(start, end - start));
      start = end + 1;
    }

    int depth = -1;
    bool hasScore = false;
    Eval candidate;

    for (size_t i = 0; i < tokens.size(); ++i) {
      if (tokens[i] == "depth" && i + 1 < tokens.size())
        depth = std::atoi(tokens[i + 1].c_str());
      else if (tokens[i] == "score" && i + 2 < tokens.size()) {
        if (tokens[i + 1] == "cp") {
          candidate.centipawns = std::atoi(tokens[i + 2].c_str());
          candidate.mateIn = 0;
          hasScore = true;
        } else if (tokens[i + 1] == "mate") {
          candidate.mateIn = std::atoi(tokens[i + 2].c_str());
          candidate.centipawns = candidate.mateIn > 0 ? 100000 : -100000;
          hasScore = true;
        }
      } else if (tokens[i] == "pv") {
        std::string pv;
        for (size_t j = i + 1; j < tokens.size(); ++j) {
          if (!pv.empty())
            pv += " ";
          pv += tokens[j];
        }
        candidate.pv = pv;
      }
    }

    if (hasScore && depth > bestDepth) {
      bestDepth = depth;
      best = candidate;
      best.depth = depth;
      best.valid = true;
    }
  }
  return best;
}

Move StockfishUCIAI::bestMove(const std::vector<Move> &legalMoves) {
  if (!m_available.load() || legalMoves.empty())
    return Move{};

  // Casual blunder rate: chance to play a random legal move for very easy
  // difficulty
  static std::mt19937 rng{std::random_device{}()};
  std::uniform_real_distribution<float> roll(0.0f, 1.0f);
  if (legalMoves.size() > 1 && roll(rng) < ChessConfig::AI_BLUNDER_CHANCE) {
    std::uniform_int_distribution<size_t> dist(0, legalMoves.size() - 1);
    return legalMoves[dist(rng)];
  }

  if (!sendLine("go depth " + std::to_string(ChessConfig::AI_SEARCH_DEPTH)))
    return legalMoves[0];

  std::string line = readUntil({"bestmove"});
  if (line.empty())
    return legalMoves[0];

  // "bestmove e2e4 ponder e7e5"
  size_t space = line.find(' ');
  std::string uci = space == std::string::npos ? line : line.substr(space + 1);
  space = uci.find(' ');
  if (space != std::string::npos)
    uci = uci.substr(0, space);

  auto parsed = ChessLibBoard::fromUci(uci);
  if (parsed) {
    // Verify against the legal move list; fall back to the first legal
    // move if the engine suggested something illegal.
    for (const auto &legal : legalMoves) {
      if (legal == *parsed)
        return legal;
    }
    // Promotion piece may differ (engine may pick e.g. rook); accept
    // any promotion move matching from/to.
    for (const auto &legal : legalMoves) {
      if (legal.from == parsed->from && legal.to == parsed->to)
        return legal;
    }
  }
  return legalMoves[0];
}

Eval StockfishUCIAI::evaluate(const std::string &fen, int movetimeMs) {
  Eval result;
  if (!m_available.load())
    return result;

  if (!sendLine("position fen " + fen))
    return result;
  if (!sendLine("go movetime " + std::to_string(movetimeMs)))
    return result;

  std::vector<std::string> infoLines;
  while (true) {
    std::string line = readUntil({"bestmove", "info"});
    if (line.empty())
      return result;
    size_t space = line.find(' ');
    std::string first =
        space == std::string::npos ? line : line.substr(0, space);
    if (first == "bestmove")
      break;
    infoLines.push_back(line);
  }

  result = parseInfoLines(infoLines);
  return result;
}

void StockfishUCIAI::shutdown() {
  if (m_shutdown.exchange(true))
    return;
  if (m_process) {
    // Write "quit" directly: sendLine() is gated on m_shutdown.
    if (m_input) {
      writeAll(m_input, "quit\n", 5);
      SDL_FlushIO(m_input);
    }
    int exitcode = 0;
    if (!SDL_WaitProcess(m_process, true, &exitcode)) {
      // Process not exiting in time: force kill.
      SDL_KillProcess(m_process, true);
      SDL_WaitProcess(m_process, true, &exitcode);
    }
    SDL_DestroyProcess(m_process);
    m_process = nullptr;
  }
  m_input = nullptr;
  m_output = nullptr;
  m_available = false;
}
} // namespace wchess
