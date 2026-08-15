#pragma once

// Detects the tactical patterns of a single move (forks, pins, skewers,
// discovered attacks, hanging pieces, checks, mates...) using the bitboard
// API of the vendored chess.hpp library. Pure functions: no game state lives
// here. The results feed the MoveClassifier and the AnnotationWriter.

#include "chess/ChessLibBoard.h"
#include "chess/ChessTypes.h"

namespace wchess
{
	namespace TacticDetector
	{
		namespace detail
		{
			// Attacks of the piece type `type` sitting on `sq` with the given
			// occupancy.
			inline chess::Bitboard attacksOf(const chess::Board& board, chess::PieceType type, chess::Square sq,
											 chess::Bitboard occ)
			{
				using namespace chess;
				switch (type.internal())
				{
					case chess::PieceType::PAWN:
						return attacks::pawn(board.sideToMove(), sq); // side to move == owner here
					case chess::PieceType::KNIGHT:
						return attacks::knight(sq);
					case chess::PieceType::BISHOP:
						return attacks::bishop(sq, occ);
					case chess::PieceType::ROOK:
						return attacks::rook(sq, occ);
					case chess::PieceType::QUEEN:
						return attacks::queen(sq, occ);
					case chess::PieceType::KING:
						return attacks::king(sq);
					default:
						return Bitboard(0);
				}
			}

			// Returns all pieces of the given color attacking sq.
			inline chess::Bitboard attackersOf(const chess::Board& board, chess::Square sq, chess::Color color)
			{
				using namespace chess;
				return (attacks::pawn(~color, sq) & board.pieces(chess::PieceType::PAWN, color)) |
					   (attacks::knight(sq) & board.pieces(chess::PieceType::KNIGHT, color)) |
					   (attacks::king(sq) & board.pieces(chess::PieceType::KING, color)) |
					   (attacks::bishop(sq, board.occ()) & (board.pieces(chess::PieceType::BISHOP, color) |
															board.pieces(chess::PieceType::QUEEN, color))) |
					   (attacks::rook(sq, board.occ()) &
						(board.pieces(chess::PieceType::ROOK, color) | board.pieces(chess::PieceType::QUEEN, color)));
			}
		} // namespace detail

		// Analyzes `move` played in position `before`. The board is copied and
		// the move applied to a scratch board; `before` is left untouched.
		inline TacticInfo analyze(const ChessLibBoard& before, const Move& move)
		{
			TacticInfo info;

			auto piece = before.pieceAt(move.from);
			auto targetPiece = before.pieceAt(move.to);
			bool isCap = move.isCapture || move.isEnPassant || targetPiece.has_value();

			info.capture = isCap;
			info.enPassant = move.isEnPassant;
			info.castling = move.isCastling;
			info.promotion = move.isPromotion;

			ChessLibBoard after = before;
			if (!after.makeMove(move))
				return info;

			const chess::Board& raw = after.raw();
			using namespace chess;

			// The mover is the side to move in the pre-move position.
			const Color moverC = before.sideToMove();
			const chess::Color moverCh = moverC == Color::White ? chess::Color::WHITE : chess::Color::BLACK;
			const chess::Color enemyCh = moverCh == chess::Color::WHITE ? chess::Color::BLACK : chess::Color::WHITE;

			// --- check / mate / draw ---
			info.check = after.inCheck();
			if (info.check)
			{
				chess::Square ksq = raw.kingSq(enemyCh);
				chess::Bitboard checkers = detail::attackersOf(raw, ksq, moverCh);
				info.doubleCheck = checkers.count() >= 2;
				CheckType ct = before.raw().givesCheck(ChessLibBoard::toInternal(move));
				info.discoveredCheck = (ct == CheckType::DISCOVERY_CHECK) || info.doubleCheck;
			}
			if (after.isGameOver())
			{
				switch (after.gameState())
				{
					case GameState::Checkmate:
						info.checkmate = true;
						break;
					case GameState::Stalemate:
						info.stalemate = true;
						break;
					default:
						info.draw = true;
						break;
				}
			}

			// --- back rank ---
			Square enemyKing = after.kingSquare(moverC == Color::White ? Color::Black : Color::White);
			if (info.check && (enemyKing.rank == 0 || enemyKing.rank == 7))
				info.backRank = true;

			// --- fork ---
			if (!move.isPromotion && !move.isCastling && piece && piece->second != PieceType::King)
			{
				chess::Square sq = chess::Square(move.to.file + move.to.rank * 8);
				chess::PieceType pt;
				switch (piece->second)
				{
					case PieceType::Pawn:
						pt = chess::PieceType::PAWN;
						break;
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
				chess::Bitboard attacked = detail::attacksOf(raw, pt, sq, raw.occ()) & raw.us(enemyCh);
				int targets = 0;
				int moverVal = pieceValue(piece->second);
				while (attacked)
				{
					chess::Square s(attacked.pop());
					Piece p = raw.at(s);
					if (p.type() == chess::PieceType::KING)
					{
						++targets;
					}
					else
					{
						int targetVal = 100;
						switch (p.type().internal())
						{
							case chess::PieceType::KNIGHT:
							case chess::PieceType::BISHOP:
								targetVal = 300;
								break;
							case chess::PieceType::ROOK:
								targetVal = 500;
								break;
							case chess::PieceType::QUEEN:
								targetVal = 900;
								break;
							default:
								targetVal = 100;
								break;
						}
						bool undefended = !raw.isAttacked(s, enemyCh);
						if (targetVal >= moverVal || undefended || targetVal >= 300)
						{
							++targets;
						}
					}
				}
				info.forkTargets = targets;
				info.fork = targets >= 2;
			}

			// --- pin / skewer: rays emanating from the moved piece ---
			if (piece && !move.isCastling)
			{
				bool isSliderDiag = piece->second == PieceType::Bishop || piece->second == PieceType::Queen;
				bool isSliderOrtho = piece->second == PieceType::Rook || piece->second == PieceType::Queen;
				if (isSliderDiag || isSliderOrtho)
				{
					chess::Square moverSq = chess::Square(move.to.file + move.to.rank * 8);
					const int dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
					for (auto& d : dirs)
					{
						bool diag = d[0] != 0 && d[1] != 0;
						bool ortho = d[0] == 0 || d[1] == 0;
						if (diag ? !isSliderDiag : !isSliderOrtho)
							continue;

						// Scan ray from moverSq along direction (d[0], d[1])
						int f = static_cast<int>(moverSq.file()) + d[0];
						int r = static_cast<int>(moverSq.rank()) + d[1];

						chess::Square firstSq{};
						while (f >= 0 && f < 8 && r >= 0 && r < 8)
						{
							chess::Square s = chess::Square(f + r * 8);
							if (raw.at(s) != chess::Piece::NONE)
							{
								firstSq = s;
								break;
							}
							f += d[0];
							r += d[1];
						}

						if (firstSq == chess::Square::underlying::NO_SQ || raw.at(firstSq).color() != enemyCh)
							continue;

						f = static_cast<int>(firstSq.file()) + d[0];
						r = static_cast<int>(firstSq.rank()) + d[1];
						chess::Square secondSq{};
						while (f >= 0 && f < 8 && r >= 0 && r < 8)
						{
							chess::Square s = chess::Square(f + r * 8);
							if (raw.at(s) != chess::Piece::NONE)
							{
								secondSq = s;
								break;
							}
							f += d[0];
							r += d[1];
						}

						if (secondSq == chess::Square::underlying::NO_SQ || raw.at(secondSq).color() != enemyCh)
							continue;

						chess::Piece firstPiece = raw.at(firstSq);
						chess::Piece secondPiece = raw.at(secondSq);

						// PIN: firstPiece is an enemy piece in front of enemy King or Queen
						if (firstPiece.type() != chess::PieceType::KING)
						{
							if (secondPiece.type() == chess::PieceType::KING ||
								(secondPiece.type() == chess::PieceType::QUEEN &&
								 firstPiece.type() != chess::PieceType::QUEEN))
							{
								info.pin = true;
							}
						}

						// SKEWER: firstPiece is enemy King (delivering check) or Queen, exposing a valuable target
						// behind it
						if (firstPiece.type() == chess::PieceType::KING && info.check)
						{
							bool targetBehindIsValuable =
								(secondPiece.type() == chess::PieceType::QUEEN ||
								 secondPiece.type() == chess::PieceType::ROOK || !raw.isAttacked(secondSq, enemyCh));
							if (targetBehindIsValuable)
								info.skewer = true;
						}
						else if (firstPiece.type() == chess::PieceType::QUEEN)
						{
							bool targetBehindIsValuable =
								(secondPiece.type() == chess::PieceType::ROOK ||
								 secondPiece.type() == chess::PieceType::BISHOP ||
								 secondPiece.type() == chess::PieceType::KNIGHT || !raw.isAttacked(secondSq, enemyCh));
							if (targetBehindIsValuable)
								info.skewer = true;
						}
					}
				}
			}

			// --- hangs piece: moved piece stands attacked and undefended / lost ---
			{
				chess::Square to = chess::Square(move.to.file + move.to.rank * 8);
				bool enemyAttacks = raw.isAttacked(to, enemyCh);
				bool moverDefends = raw.isAttacked(to, moverCh);

				if (enemyAttacks && !after.inCheck())
				{
					int moverVal = piece ? pieceValue(piece->second) : 100;
					int capturedVal = 0;
					if (isCap)
					{
						if (move.isEnPassant)
						{
							capturedVal = 100;
						}
						else if (targetPiece)
						{
							capturedVal = pieceValue(targetPiece->second);
						}
						else
						{
							capturedVal = 100;
						}
					}

					// Only hanging if this was not an equal or winning capture
					if (!isCap || moverVal > capturedVal)
					{
						if (!moverDefends)
						{
							info.hangsPiece = true;
						}
					}
				}
			}

			// --- discovered attack: a friendly slider attacks something new ---
			{
				chess::Bitboard friendlySliders =
					raw.pieces(chess::PieceType::BISHOP, chess::PieceType::ROOK, chess::PieceType::QUEEN) &
					raw.us(moverCh);
				chess::Bitboard enemyPieces = raw.us(enemyCh);
				chess::Bitboard occNoMover = raw.occ();
				occNoMover.clear(chess::Square(chess::File(move.from.file), chess::Rank(move.from.rank)).index());

				chess::Bitboard attackersNoMover = 0;
				chess::Bitboard attackersWithMover = 0;
				while (friendlySliders)
				{
					chess::Square sq(friendlySliders.pop());
					chess::Piece p = raw.at(sq);
					chess::Bitboard withOcc = detail::attacksOf(raw, p.type(), sq, raw.occ());
					chess::Bitboard noOcc = detail::attacksOf(raw, p.type(), sq, occNoMover);
					attackersNoMover |= noOcc & enemyPieces;
					attackersWithMover |= withOcc & enemyPieces;
				}
				if ((attackersNoMover & ~attackersWithMover))
					info.discoveredAttack = true;
			}

			return info;
		}
	} // namespace TacticDetector
} // namespace wchess
