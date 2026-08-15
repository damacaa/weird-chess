#pragma once

// Turns MoveAnnotation data into human-readable chess.com-style text. All
// strings respect the engine font charset (see README): A-Z, a-z, 0-9 and
// !"&_*()-=+?|.,:; - no '#', no quotes, no unicode.

#include "chess/ChessTypes.h"

#include <string>

namespace wchess
{
	namespace AnnotationWriter
	{
		// Builds the SAN-like notation for a move given its moving piece.
		// The piece type must be provided by the caller (board lookup).
		inline std::string notation(const Move& move, const char* pieceLetter, bool capture, bool check, bool mate,
									bool promotion, PieceType promoType)
		{
			std::string s;
			if (move.isCastling)
			{
				s = (move.to.file > move.from.file) ? "O-O" : "O-O-O";
			}
			else
			{
				s = pieceLetter;
				if (capture)
				{
					if (s.empty())
						s += static_cast<char>('a' + move.from.file);
					s += "x";
				}
				s += move.to.algebraic();
				if (promotion)
				{
					s += "=";
					s += pieceChar(promoType);
				}
			}
			if (mate)
				s += " mate";
			else if (check)
				s += "+";
			return s;
		}

		// Title: the primary label for the move, e.g. "BLUNDER", "FORK",
		// "CHECKMATE", "BEST MOVE".
		inline std::string title(const MoveAnnotation& ann)
		{
			const TacticInfo& t = ann.tactics;
			const char* quality = qualityName(ann.quality);

			if (t.checkmate)
				return "CHECKMATE";
			if (t.stalemate)
				return "STALEMATE";
			if (t.draw)
				return "DRAW";
			if (t.fork)
				return "FORK";
			if (t.pin)
				return "PIN";
			if (t.skewer)
				return "SKEWER";
			if (t.discoveredAttack)
				return "DISCOVERED ATTACK";
			if (t.discoveredCheck)
				return "DISCOVERED CHECK";
			if (t.doubleCheck)
				return "DOUBLE CHECK";
			if (t.check)
				return "CHECK";
			if (t.hangsPiece)
				return "HANGING PIECE";

			return quality;
		}

		// Direct, factual chess summary (zero literary expressions).
		inline std::string summary(const MoveAnnotation& ann)
		{
			const TacticInfo& t = ann.tactics;
			const std::string san = ann.san.empty() ? "-" : ann.san;
			const std::string mover = ann.mover == Color::White ? "White" : "Black";
			const std::string enemy = ann.mover == Color::White ? "Black" : "White";

			if (t.checkmate)
				return mover + " delivers checkmate with " + san + ". Game over.";
			if (t.stalemate)
				return "Stalemate. " + enemy + " has no legal moves. Game drawn.";
			if (t.draw)
				return "Game ended in a draw.";

			if (t.fork)
				return mover + " plays " + san + " with a fork.";
			if (t.pin)
				return mover + " plays " + san + " with a pin.";
			if (t.skewer)
				return mover + " plays " + san + " with a skewer.";
			if (t.discoveredCheck)
				return mover + " plays " + san + " with discovered check.";
			if (t.doubleCheck)
				return mover + " plays " + san + " with double check.";
			if (t.discoveredAttack)
				return mover + " plays " + san + " with discovered attack.";
			if (t.hangsPiece)
				return mover + " plays " + san + ", leaving piece undefended.";

			if (ann.quality == MoveQuality::Blunder)
				return mover + " blundered with " + san + ".";
			if (ann.quality == MoveQuality::Mistake)
				return mover + " made a mistake with " + san + ".";
			if (ann.quality == MoveQuality::Miss)
				return mover + " missed best continuation with " + san + ".";
			if (ann.quality == MoveQuality::Inaccuracy)
				return mover + " played inaccurate move " + san + ".";
			if (ann.quality == MoveQuality::Brilliant)
				return mover + " found brilliant move " + san + "!";
			if (ann.quality == MoveQuality::Great)
				return mover + " played great move " + san + ".";
			if (ann.quality == MoveQuality::Excellent)
				return mover + " played excellent move " + san + ".";
			if (ann.quality == MoveQuality::Forced)
				return mover + " forced to play " + san + ".";
			if (ann.quality == MoveQuality::Best)
				return mover + " played best move " + san + ".";

			return mover + " played " + san + ".";
		}

		// Special tactical event message for forks, pins, skewers, blunders, brilliant
		// moves, hanging pieces.
		inline std::string specialEvent(const MoveAnnotation& ann)
		{
			const TacticInfo& t = ann.tactics;
			const std::string mover = ann.mover == Color::White ? "White" : "Black";
			const std::string enemy = ann.mover == Color::White ? "Black" : "White";
			const std::string pieceName = pieceTypeName(ann.pieceMoved);
			const std::string imp = impactLevelName(ann.impact);

			if (t.checkmate)
				return "GAME END: CHECKMATE - " + mover + " delivers checkmate with " + ann.san + ".";
			if (t.stalemate)
			{
				if (ann.impact == ImpactLevel::Critical || ann.winChanceBefore >= 0.70f)
					return "CRITICAL BLUNDER: STALEMATE - " + mover + " accidentally stalemates " + enemy + " with " +
						   ann.san + ". Victory thrown away into a draw!";
				return "GAME END: STALEMATE - " + mover + " stalemates " + enemy + " with " + ann.san +
					   ". The match ends in a draw.";
			}
			if (t.draw)
				return "GAME END: DRAW - The game concludes in a draw by rule.";

			if (ann.quality == MoveQuality::Brilliant)
				return "CRITICAL EVENT: BRILLIANT MOVE - " + mover + " executes a deep tactical sacrifice with " +
					   ann.san + "!";

			if (ann.quality == MoveQuality::Blunder)
			{
				if (ann.impact == ImpactLevel::Critical)
				{
					return "CRITICAL BLUNDER: Catastrophic disaster! " + mover + " blunders with " + ann.san +
						   ", collapsing win probability and throwing away the match.";
				}
				if (ann.impact == ImpactLevel::Major)
				{
					return "MAJOR BLUNDER: Heavy setback. " + mover + " commits a serious blunder with " + ann.san +
						   ", swinging significant advantage to " + enemy + ".";
				}
				return "MINOR BLUNDER: " + mover + " blunders with " + ann.san +
					   ", though the game balance was already heavily decided.";
			}

			if (ann.quality == MoveQuality::Mistake)
			{
				if (ann.impact >= ImpactLevel::Major)
				{
					return "MAJOR MISTAKE: Significant error. " + mover + " plays " + ann.san +
						   ", conceding valuable ground.";
				}
				return "MINOR MISTAKE: " + mover + " plays " + ann.san + " with a minor positional concession.";
			}

			if (t.fork)
				return imp + " TACTIC: FORK - " + mover + " " + pieceName + " on " + ann.move.to.algebraic() +
					   " attacks multiple enemy pieces simultaneously.";
			if (t.pin)
				return imp + " TACTIC: PIN - " + mover + " " + pieceName + " on " + ann.move.to.algebraic() +
					   " pins an enemy piece against the King.";
			if (t.skewer)
				return imp + " TACTIC: SKEWER - " + mover + " " + pieceName + " on " + ann.move.to.algebraic() +
					   " skewers the King and an exposed piece behind it.";
			if (t.discoveredCheck)
				return imp + " TACTIC: DISCOVERED CHECK - " + mover +
					   " move uncovers a direct line of fire against the King.";
			if (t.doubleCheck)
				return imp + " TACTIC: DOUBLE CHECK - " + mover + " attacks the King with two pieces simultaneously.";
			if (t.discoveredAttack)
				return imp + " TACTIC: DISCOVERED ATTACK - " + mover + " uncovers an attack on an enemy target.";
			if (t.hangsPiece)
				return imp + " TACTIC: HANGING PIECE - " + mover + " left " + pieceName + " on " +
					   ann.move.to.algebraic() + " undefended.";

			return "";
		}

		// Trade / Exchange evaluation message (trading blows, favorable/unfavorable
		// trades, queen trades, forced trades, or free material won).
		inline std::string tradeEvent(const MoveAnnotation& ann)
		{
			if (!ann.isTrade && !ann.hasCapture)
				return "";

			const std::string mover = ann.mover == Color::White ? "White" : "Black";
			const std::string enemy = ann.mover == Color::White ? "Black" : "White";
			const std::string pieceName = pieceTypeName(ann.pieceMoved);
			const std::string capturedName = pieceTypeName(ann.pieceCaptured);
			const std::string imp = impactLevelName(ann.impact);

			if (ann.isQueenTrade)
			{
				if (ann.impact == ImpactLevel::Critical)
					return "CRITICAL TRADE: Queens traded off the board! Decisive transition "
						   "that cements the outcome.";
				if (ann.impact == ImpactLevel::Major)
					return "MAJOR TRADE: Queens traded off the board. The heavy leaders "
						   "leave the battlefield.";
				return "MINOR TRADE: Queens traded off the board in an already settled "
					   "position.";
			}

			if (ann.isRecapture)
			{
				if (ann.impact == ImpactLevel::Critical)
					return "CRITICAL EXCHANGE: Trading blows! " + mover + " executes a game-deciding recapture on " +
						   ann.move.to.algebraic() + " with " + ann.san + ".";
				if (ann.impact == ImpactLevel::Major)
					return "MAJOR EXCHANGE: Trading blows! " + mover + " immediately recaptures on " +
						   ann.move.to.algebraic() + " with " + ann.san + ".";
				return "MINOR EXCHANGE: " + mover + " recaptures on " + ann.move.to.algebraic() + " with " + ann.san +
					   ".";
			}

			if (ann.isTrade)
			{
				if (ann.impact == ImpactLevel::Critical)
					return "CRITICAL TRADE: High-stakes trade! " + mover + " trades " + pieceName + " for " +
						   capturedName + " on " + ann.move.to.algebraic() + ", sealing the advantage.";
				if (ann.impact == ImpactLevel::Major)
					return "MAJOR TRADE: Strategic piece trade. " + mover + " trades " + pieceName + " for " +
						   capturedName + " on " + ann.move.to.algebraic() + ".";

				return "MINOR TRADE: Routine piece trade. " + mover + " exchanges " + pieceName + " for " +
					   capturedName + " on " + ann.move.to.algebraic() + ".";
			}

			// Free capture / winning material (ann.hasCapture is true, but !ann.isTrade)
			if (ann.hasCapture)
			{
				if (ann.pieceCaptured == PieceType::Pawn)
					return "MATERIAL: " + mover + " captures an undefended Pawn on " + ann.move.to.algebraic() + ".";
				return "MATERIAL: " + mover + " wins a free " + capturedName + " on " + ann.move.to.algebraic() + "!";
			}

			return "";
		}

		// Brief status evaluating the whole game state (who is in a tight spot, tables
		// turned, balanced, etc.)
		inline std::string gameStatus(const MoveAnnotation& ann)
		{
			const std::string mover = ann.mover == Color::White ? "White" : "Black";
			const std::string enemy = ann.mover == Color::White ? "Black" : "White";

			// 1. Check for a major momentum shift / tables turned
			// evalBeforeCp and evalAfterCp are from the MOVER'S perspective.
			bool moverWasLeading = ann.evalBeforeCp > 150;
			bool enemyWasLeading = ann.evalBeforeCp < -150;
			bool moverNowLeading = ann.evalAfterCp > 150;
			bool enemyNowLeading = ann.evalAfterCp < -150;

			if (enemyWasLeading && moverNowLeading)
				return "MOMENTUM SHIFT: The tables have turned. " + enemy + " held the lead, but " + mover +
					   " now seizes full control.";
			if (moverWasLeading && enemyNowLeading)
				return "MOMENTUM SHIFT: The tables have turned. " + mover + " held the lead, but " + enemy +
					   " now seizes full control.";

			// 2. Periodic evaluation every 4 full moves (at the end of Black turn)
			// Don't claim "holding firm" if a mistake/blunder just occurred on this move
			if (ann.fullMoveNumber >= 4 && ann.fullMoveNumber % 4 == 0 && ann.mover == Color::Black &&
				ann.quality != MoveQuality::Blunder && ann.quality != MoveQuality::Mistake)
			{
				// Mover is Black, so positive evalPawns means Black is leading, negative
				// means White is leading.
				float evalPawns = static_cast<float>(ann.evalAfterCp) / 100.0f;
				if (evalPawns >= 3.0f)
					return "GAME STATUS: White is in a tight spot under heavy pressure. "
						   "Black holds a decisive advantage.";
				if (evalPawns <= -3.0f)
					return "GAME STATUS: Black is in a tight spot under heavy pressure. "
						   "White holds a decisive advantage.";
				if (evalPawns >= 0.8f)
					return "GAME STATUS: Black holds a solid upper hand. White is forced on "
						   "the defensive.";
				if (evalPawns <= -0.8f)
					return "GAME STATUS: White holds a solid upper hand. Black is forced on "
						   "the defensive.";
				if (std::abs(ann.winChanceDelta) < 0.10f)
					return "GAME STATUS: Balanced position. Both players are holding firm with "
						   "equal chances.";
			}

			return "";
		}

		// Formats a short, direct, semantic event string optimized for a small (3B)
		// LLM. Token-efficient (~20-30 tokens), unambiguous chess facts with zero
		// literary fluff.
		inline std::string formatLLMEvent(const MoveAnnotation& ann)
		{
			const std::string mover = ann.mover == Color::White ? "White" : "Black";
			const std::string enemy = ann.mover == Color::White ? "Black" : "White";

			std::string s = "Move " + std::to_string(ann.fullMoveNumber) + " (" + mover + "): " + ann.san + ". ";

			if (ann.move.isCastling)
			{
				s += (ann.move.to.file > ann.move.from.file) ? "Castles kingside. " : "Castles queenside. ";
			}
			else
			{
				s += std::string(pieceTypeName(ann.pieceMoved)) + " " + ann.move.from.algebraic() + " to " +
					 ann.move.to.algebraic() + ". ";
				if (ann.hasCapture)
				{
					s += "Captures " + enemy + " " + std::string(pieceTypeName(ann.pieceCaptured)) + ". ";
				}
				if (ann.move.isPromotion)
				{
					s += "Promotes to " + std::string(pieceTypeName(ann.move.promotion)) + ". ";
				}
			}

			if (ann.tactics.checkmate)
				s += "Checkmate. " + mover + " wins. ";
			else if (ann.tactics.check)
				s += "Check. ";

			s += "Quality: " + std::string(qualityName(ann.quality)) + ". ";

			int moverWinPct = static_cast<int>(std::round(ann.winChanceAfter * 100.0f));
			int enemyWinPct = 100 - moverWinPct;
			int shiftPct = static_cast<int>(std::round(ann.winChanceDelta * 100.0f));

			char winBuf[64];
			std::snprintf(winBuf, sizeof(winBuf), "Win: %s %d - %s %d (Shift: %+d, %s).", mover.c_str(), moverWinPct,
						  enemy.c_str(), enemyWinPct, shiftPct, impactLevelName(ann.impact));
			s += winBuf;

			bool hasMajorTacticalEvent =
				(ann.quality == MoveQuality::Brilliant || ann.quality == MoveQuality::Blunder || ann.tactics.checkmate);

			if (!ann.specialEvent.empty())
				s += " " + ann.specialEvent;

			// Do not contradict a brilliant strike or blunder with a routine trade event
			if (!hasMajorTacticalEvent && !ann.tradeEvent.empty())
				s += " " + ann.tradeEvent;

			if (ann.quality != MoveQuality::Blunder && !ann.gameStatus.empty())
				s += " " + ann.gameStatus;

			return s;
		}

		// Fills ann.san, ann.title, ann.summary, ann.specialEvent, ann.tradeEvent,
		// ann.gameStatus and ann.engineLine. pieceLetter: "N", "B", "R", "Q", "K" or ""
		// for pawns.
		inline void decorate(MoveAnnotation& ann, const char* pieceLetter)
		{
			ann.san = notation(ann.move, pieceLetter, ann.tactics.capture, ann.tactics.check, ann.tactics.checkmate,
							   ann.tactics.promotion, ann.move.promotion);
			ann.title = title(ann);
			ann.summary = summary(ann);
			ann.specialEvent = specialEvent(ann);
			ann.tradeEvent = tradeEvent(ann);
			ann.gameStatus = gameStatus(ann);

			// Engine line: only keep first 2 moves for brief reference.
			if (!ann.engineLine.empty())
			{
				size_t pos = 0;
				int spaces = 0;
				while (pos < ann.engineLine.size() && spaces < 2)
				{
					if (ann.engineLine[pos] == ' ')
						++spaces;
					if (spaces < 2)
						++pos;
				}
				ann.engineLine = "line: " + ann.engineLine.substr(0, pos);
			}
			else
			{
				ann.engineLine = "";
			}
		}

		inline std::string formatScore(int cp)
		{
			if (cp >= 50000)
				return "Mate";
			if (cp <= -50000)
				return "-Mate";
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%+.1f", static_cast<float>(cp) / 100.0f);
			return buf;
		}

		// Formats the eval delta and win chance for the panel, e.g. "eval +1.2 -> +0.4
		// (win 68 to 58 pct, MINOR)".
		inline std::string evalText(const MoveAnnotation& ann)
		{
			int winBeforePct = static_cast<int>(std::round(ann.winChanceBefore * 100.0f));
			int winAfterPct = static_cast<int>(std::round(ann.winChanceAfter * 100.0f));
			std::string beforeStr = formatScore(ann.evalBeforeCp);
			std::string afterStr = formatScore(ann.evalAfterCp);
			char buf[96];
			std::snprintf(buf, sizeof(buf), "eval %s -> %s, win %d to %d pct (%s)", beforeStr.c_str(), afterStr.c_str(),
						  winBeforePct, winAfterPct, impactLevelName(ann.impact));
			return buf;
		}
	} // namespace AnnotationWriter
} // namespace wchess
