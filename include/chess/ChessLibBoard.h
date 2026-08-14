#pragma once

// The rules layer: canonical game state on top of the vendored chess.hpp
// (Disservin/chess-library, MIT). This class is the single source of truth
// for the position. The IChessAI implementations are only allowed to evaluate
// positions and pick moves - they never mutate the game.

#include "chess.hpp"
#include "chess/ChessTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace wchess {
class ChessLibBoard {
public:
  ChessLibBoard() = default;

  void loadStartPosition() {
    m_board = chess::Board(chess::constants::STARTPOS);
  }

  bool setFen(const std::string &fen) { return m_board.setFen(fen); }

  std::string getFen() const { return m_board.getFen(); }

  Color sideToMove() const {
    return m_board.sideToMove() == chess::Color::WHITE ? Color::White
                                                       : Color::Black;
  }

  bool inCheck() const { return m_board.inCheck(); }

  // True if the move is legal in the current position.
  bool isLegal(const Move &move) const {
    chess::Move internal = toInternal(move);
    if (internal != chess::Move::NO_MOVE && m_board.isLegal(internal))
      return true;
    for (const auto &legal : legalMoves()) {
      if (legal.from == move.from && legal.to == move.to &&
          (!move.isPromotion || legal.promotion == move.promotion))
        return true;
    }
    return false;
  }

  // Applies the move. Returns false if it was not legal (board untouched).
  bool makeMove(const Move &move) {
    chess::Move internal = toInternal(move);
    if (internal == chess::Move::NO_MOVE || !m_board.isLegal(internal)) {
      bool found = false;
      for (const auto &legal : legalMoves()) {
        if (legal.from == move.from && legal.to == move.to &&
            (!move.isPromotion || legal.promotion == move.promotion)) {
          internal = toInternal(legal);
          found = true;
          break;
        }
      }
      if (!found || internal == chess::Move::NO_MOVE ||
          !m_board.isLegal(internal))
        return false;
    }
    bool wasCapture = m_board.isCapture(internal);
    m_board.makeMove(internal);
    recordHistory(internal, wasCapture);
    return true;
  }

  // Undoes the last move (used by the annotation pipeline).
  void undoMove() {
    // chess.hpp tracks history internally; unmakeMove must be passed
    // the move that was made. We keep a small stack instead.
    if (m_history.empty())
      return;
    m_board.unmakeMove(m_history.back().internal);
    m_history.pop_back();
  }

  // The move that was last applied (empty optional if none).
  std::optional<Move> lastMove() const {
    return m_history.empty() ? std::nullopt
                             : std::optional<Move>(m_history.back().external);
  }

  std::vector<Move> legalMoves() const {
    std::vector<Move> result;
    chess::Movelist list;
    chess::movegen::legalmoves(list, m_board);
    result.reserve(static_cast<size_t>(list.size()));
    for (const auto &m : list) {
      Move converted = toExternal(m);
      converted.isCapture = m_board.isCapture(m);
      if (converted.from.valid() && converted.to.valid())
        result.push_back(converted);
    }
    return result;
  }

  std::vector<Move> legalMovesFrom(const Square &square) const {
    std::vector<Move> result;
    for (const auto &move : legalMoves()) {
      if (move.from == square)
        result.push_back(move);
    }
    return result;
  }

  std::optional<std::pair<Color, PieceType>>
  pieceAt(const Square &square) const {
    chess::Piece p = m_board.at(
        chess::Square(chess::File(square.file), chess::Rank(square.rank)));
    if (p == chess::Piece::NONE)
      return std::nullopt;
    PieceType type;
    switch (p.type().internal()) {
    case chess::PieceType::PAWN:
      type = PieceType::Pawn;
      break;
    case chess::PieceType::KNIGHT:
      type = PieceType::Knight;
      break;
    case chess::PieceType::BISHOP:
      type = PieceType::Bishop;
      break;
    case chess::PieceType::ROOK:
      type = PieceType::Rook;
      break;
    case chess::PieceType::QUEEN:
      type = PieceType::Queen;
      break;
    default:
      type = PieceType::King;
      break;
    }
    return std::pair<Color, PieceType>(
        p.color() == chess::Color::WHITE ? Color::White : Color::Black, type);
  }

  Square kingSquare(Color color) const {
    chess::Square sq = m_board.kingSq(
        color == Color::White ? chess::Color::WHITE : chess::Color::BLACK);
    return Square{static_cast<int>(sq.file()), static_cast<int>(sq.rank())};
  }

  bool isGameOver() const {
    auto [reason, result] = m_board.isGameOver();
    return result != chess::GameResult::NONE;
  }

  GameState gameState() const {
    auto [reason, result] = m_board.isGameOver();
    switch (reason) {
    case chess::GameResultReason::CHECKMATE:
      return GameState::Checkmate;
    case chess::GameResultReason::STALEMATE:
      return GameState::Stalemate;
    case chess::GameResultReason::INSUFFICIENT_MATERIAL:
      return GameState::InsufficientMaterial;
    case chess::GameResultReason::FIFTY_MOVE_RULE:
      return GameState::FiftyMoveRule;
    case chess::GameResultReason::THREEFOLD_REPETITION:
      return GameState::ThreefoldRepetition;
    default:
      return GameState::Ongoing;
    }
  }

  int halfMoveClock() const {
    return static_cast<int>(m_board.halfMoveClock());
  }

  int fullMoveNumber() const {
    return static_cast<int>(m_board.fullMoveNumber());
  }

  // ---- internal access for the tactic detector ----
  const chess::Board &raw() const { return m_board; }

  static chess::Move toInternal(const Move &move) {
    if (!move.from.valid() || !move.to.valid())
      return chess::Move::NO_MOVE;

    chess::Square from(chess::File(move.from.file),
                       chess::Rank(move.from.rank));
    chess::Square to(chess::File(move.to.file), chess::Rank(move.to.rank));

    if (move.isPromotion) {
      chess::PieceType pt = chess::PieceType::QUEEN;
      switch (move.promotion) {
      case PieceType::Knight:
        pt = chess::PieceType::KNIGHT;
        break;
      case PieceType::Bishop:
        pt = chess::PieceType::BISHOP;
        break;
      case PieceType::Rook:
        pt = chess::PieceType::ROOK;
        break;
      default:
        pt = chess::PieceType::QUEEN;
        break;
      }
      return chess::Move::make<chess::Move::PROMOTION>(from, to, pt);
    }

    if (move.isCastling) {
      chess::File rookFile = (move.to.file >= move.from.file)
                                 ? chess::File::FILE_H
                                 : chess::File::FILE_A;
      chess::Square rookSq(rookFile, chess::Rank(move.from.rank));
      return chess::Move::make<chess::Move::CASTLING>(from, rookSq);
    }

    if (move.isEnPassant) {
      return chess::Move::make<chess::Move::ENPASSANT>(from, to);
    }

    return chess::Move::make(from, to);
  }

  static Move toExternal(const chess::Move &m) {
    Move out;
    out.from = Square{static_cast<int>(m.from().file()),
                      static_cast<int>(m.from().rank())};
    out.to = Square{static_cast<int>(m.to().file()),
                    static_cast<int>(m.to().rank())};

    uint16_t type = m.typeOf();
    if (type == chess::Move::CASTLING) {
      out.isCastling = true;
      int targetFile = (m.to().file() > m.from().file()) ? 6 : 2;
      out.to = Square{targetFile, static_cast<int>(m.from().rank())};
    } else if (type == chess::Move::ENPASSANT) {
      out.isEnPassant = true;
    } else if (type == chess::Move::PROMOTION) {
      out.isPromotion = true;
      switch (m.promotionType().internal()) {
      case chess::PieceType::KNIGHT:
        out.promotion = PieceType::Knight;
        break;
      case chess::PieceType::BISHOP:
        out.promotion = PieceType::Bishop;
        break;
      case chess::PieceType::ROOK:
        out.promotion = PieceType::Rook;
        break;
      default:
        out.promotion = PieceType::Queen;
        break;
      }
    }

    return out;
  }

  // UCI notation for a move, e.g. "e2e4" or "e7e8q".
  static std::string toUci(const Move &move) {
    std::string s = move.from.algebraic() + move.to.algebraic();
    if (move.isPromotion) {
      switch (move.promotion) {
      case PieceType::Knight:
        s += 'n';
        break;
      case PieceType::Bishop:
        s += 'b';
        break;
      case PieceType::Rook:
        s += 'r';
        break;
      default:
        s += 'q';
        break;
      }
    }
    return s;
  }

  // Parses UCI notation ("e2e4", "e7e8q") into a Move.
  static std::optional<Move> fromUci(const std::string &uci) {
    if (uci.size() < 4 || uci.size() > 5)
      return std::nullopt;
    Move m;
    m.from.file = uci[0] - 'a';
    m.from.rank = uci[1] - '1';
    m.to.file = uci[2] - 'a';
    m.to.rank = uci[3] - '1';
    if (!m.from.valid() || !m.to.valid())
      return std::nullopt;
    if (uci.size() == 5) {
      m.isPromotion = true;
      switch (uci[4]) {
      case 'n':
        m.promotion = PieceType::Knight;
        break;
      case 'b':
        m.promotion = PieceType::Bishop;
        break;
      case 'r':
        m.promotion = PieceType::Rook;
        break;
      default:
        m.promotion = PieceType::Queen;
        break;
      }
    }
    return m;
  }

private:
  // The piece type flags (capture/castling/en passant) are reconstructed
  // lazily because they depend on the position at move time.
  void recordHistory(const chess::Move &internal, bool wasCapture) {
    Move external = toExternal(internal);
    external.isCapture = wasCapture;
    m_history.push_back({internal, external});
  }

  struct HistoryEntry {
    chess::Move internal;
    Move external;
  };

  chess::Board m_board;
  std::vector<HistoryEntry> m_history;
};
} // namespace wchess
