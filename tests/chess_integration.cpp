// Gameplay integration test: plays a scripted game against the AI through the
// components the scene uses (ChessState, ChessLibBoard, IChessAI, annotation
// pipeline, NarratorThread + PassThroughNarrator) and checks the annotations,
// story stream and game-over flow. No window/ECS required (the ECS-bound
// pieces - moveSystem/inputSystem - are exercised by the running game).
//
// Build: cmake -B build && cmake --build build -j &&
// ./build/tests/WeirdChessTests

#include "chess/AsyncAnnotator.h"
#include "chess/MinimaxAI.h"
#include "chess/NullAI.h"
#include "chess/StockfishUCIAI.h"
#include "components/ChessState.h"
#include "config.h"
#include "globals.h"
#include "systems/animationSystem.h"
#include "systems/annotationSystem.h"
#include "systems/narrativeRenderSystem.h"

#include <cstdio>

using namespace wchess;

static int failures = 0;
#define CHECK(cond)                                                                                                    \
	do                                                                                                                 \
	{                                                                                                                  \
		if (!(cond))                                                                                                   \
		{                                                                                                              \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			++failures;                                                                                                \
		}                                                                                                              \
	} while (0)

static void testMinimaxAI()
{
	printf("---- testing MinimaxAI ----\n");
	MinimaxAI ai;
	ai.setStrength(20, 2500);
	CHECK(ai.isAvailable());
	CHECK(ai.name() == "MinimaxAI (in-process)");

	// 1. Starting position evaluation
	Eval startEval = ai.evaluate(chess::constants::STARTPOS, 100);
	CHECK(startEval.valid);
	CHECK(startEval.centipawns >= -100 && startEval.centipawns <= 100);

	// 2. Material advantage evaluation (White has queen, Black does not)
	std::string queenAdvantageFen = "rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
	Eval advEval = ai.evaluate(queenAdvantageFen, 100);
	CHECK(advEval.valid);
	CHECK(advEval.centipawns >= 700);

	// 3. Mate in 1 detection (Scholar's mate threat on f7)
	std::string mateInOneFen = "r1bqkb1r/pppp1ppp/2n5/4p3/2B1n3/5Q2/PPPP1PPP/RNB1K1NR w KQkq - 0 5";
	ChessLibBoard mateBoard;
	mateBoard.setFen(mateInOneFen);
	ai.setPosition(mateInOneFen);
	Move mateMove = ai.bestMove(mateBoard.legalMoves());
	CHECK(ChessLibBoard::toUci(mateMove) == "f3f7");

	// 4. Tactical capture of free piece (Rook takes hanging Queen)
	std::string captureFen = "4k3/8/8/3q4/3R4/8/8/4K3 w - - 0 1";
	ChessLibBoard capBoard;
	capBoard.setFen(captureFen);
	ai.setPosition(captureFen);
	Move capMove = ai.bestMove(capBoard.legalMoves());
	CHECK(ChessLibBoard::toUci(capMove) == "d4d5");

	printf("MinimaxAI unit checks passed\n");
}

static void testCastling()
{
	printf("---- testing castling ----\n");
	ChessLibBoard board;

	// Position with both sides having full castling rights and open ranks
	board.setFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
	auto whiteLegal = board.legalMoves();

	bool foundO_O = false;
	bool foundO_O_O = false;
	for (const auto& m : whiteLegal)
	{
		if (m.from == Square{4, 0} && m.to == Square{6, 0} && m.isCastling)
			foundO_O = true;
		if (m.from == Square{4, 0} && m.to == Square{2, 0} && m.isCastling)
			foundO_O_O = true;
	}
	CHECK(foundO_O);
	CHECK(foundO_O_O);

	// Test White Kingside Castling: e1g1
	Move castleK;
	castleK.from = Square{4, 0};
	castleK.to = Square{6, 0};
	castleK.isCastling = true;
	CHECK(board.isLegal(castleK));
	CHECK(ChessLibBoard::toUci(castleK) == "e1g1");

	TacticInfo tInfo = TacticDetector::analyze(board, castleK);
	CHECK(tInfo.castling);
	std::string sanK = AnnotationWriter::notation(castleK, "K", false, false, false, false, PieceType::Queen);
	CHECK(sanK == "O-O");

	CHECK(board.makeMove(castleK));
	auto kingPiece = board.pieceAt(Square{6, 0});
	auto rookPiece = board.pieceAt(Square{5, 0});
	CHECK(kingPiece && kingPiece->second == PieceType::King && kingPiece->first == Color::White);
	CHECK(rookPiece && rookPiece->second == PieceType::Rook && rookPiece->first == Color::White);
	CHECK(!board.pieceAt(Square{4, 0}));
	CHECK(!board.pieceAt(Square{7, 0}));

	// Test Black Kingside Castling: e8g8 on a fresh position where f8 is not
	// attacked
	ChessLibBoard bBoard;
	bBoard.setFen("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1");
	Move bCastleK;
	bCastleK.from = Square{4, 7};
	bCastleK.to = Square{6, 7};
	bCastleK.isCastling = true;
	CHECK(bBoard.isLegal(bCastleK));
	CHECK(ChessLibBoard::toUci(bCastleK) == "e8g8");
	std::string bSanK = AnnotationWriter::notation(bCastleK, "K", false, false, false, false, PieceType::Queen);
	CHECK(bSanK == "O-O");
	CHECK(bBoard.makeMove(bCastleK));

	auto bKing = bBoard.pieceAt(Square{6, 7});
	auto bRook = bBoard.pieceAt(Square{5, 7});
	CHECK(bKing && bKing->second == PieceType::King && bKing->first == Color::Black);
	CHECK(bRook && bRook->second == PieceType::Rook && bRook->first == Color::Black);

	// Test Black Queenside Castling: e8c8
	bBoard.setFen("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1");
	Move bCastleQ;
	bCastleQ.from = Square{4, 7};
	bCastleQ.to = Square{2, 7};
	bCastleQ.isCastling = true;
	CHECK(bBoard.isLegal(bCastleQ));
	CHECK(ChessLibBoard::toUci(bCastleQ) == "e8c8");
	std::string bSanQ = AnnotationWriter::notation(bCastleQ, "K", false, false, false, false, PieceType::Queen);
	CHECK(bSanQ == "O-O-O");
	CHECK(bBoard.makeMove(bCastleQ));

	auto bKingQ = bBoard.pieceAt(Square{2, 7});
	auto bRookQ = bBoard.pieceAt(Square{3, 7});
	CHECK(bKingQ && bKingQ->second == PieceType::King && bKingQ->first == Color::Black);
	CHECK(bRookQ && bRookQ->second == PieceType::Rook && bRookQ->first == Color::Black);

	// Test Undo
	bBoard.undoMove();
	CHECK(!bBoard.pieceAt(Square{2, 7}));
	CHECK(!bBoard.pieceAt(Square{3, 7}));
	auto bKingOrig = bBoard.pieceAt(Square{4, 7});
	auto bRookOrig = bBoard.pieceAt(Square{0, 7});
	CHECK(bKingOrig && bKingOrig->second == PieceType::King);
	CHECK(bRookOrig && bRookOrig->second == PieceType::Rook);

	board.undoMove(); // Undo White move
	auto wKingOrig = board.pieceAt(Square{4, 0});
	auto wRookOrig = board.pieceAt(Square{7, 0});
	CHECK(wKingOrig && wKingOrig->second == PieceType::King);
	CHECK(wRookOrig && wRookOrig->second == PieceType::Rook);

	// Test White Queenside Castling: e1c1
	Move castleQ;
	castleQ.from = Square{4, 0};
	castleQ.to = Square{2, 0};
	castleQ.isCastling = true;
	CHECK(board.isLegal(castleQ));
	CHECK(ChessLibBoard::toUci(castleQ) == "e1c1");
	std::string sanQ = AnnotationWriter::notation(castleQ, "K", false, false, false, false, PieceType::Queen);
	CHECK(sanQ == "O-O-O");

	CHECK(board.makeMove(castleQ));
	auto kingQ = board.pieceAt(Square{2, 0});
	auto rookQ = board.pieceAt(Square{3, 0});
	CHECK(kingQ && kingQ->second == PieceType::King && kingQ->first == Color::White);
	CHECK(rookQ && rookQ->second == PieceType::Rook && rookQ->first == Color::White);
	CHECK(!board.pieceAt(Square{4, 0}));
	CHECK(!board.pieceAt(Square{0, 0}));

	// Test fromUci and makeMove without isCastling flag
	board.setFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
	auto parsedUci = ChessLibBoard::fromUci("e1g1");
	CHECK(parsedUci.has_value());
	CHECK(board.isLegal(*parsedUci));
	CHECK(board.makeMove(*parsedUci));
	CHECK(board.pieceAt(Square{6, 0}) && board.pieceAt(Square{6, 0})->second == PieceType::King);
	CHECK(board.pieceAt(Square{5, 0}) && board.pieceAt(Square{5, 0})->second == PieceType::Rook);

	// Test castling prevented in check
	board.setFen("r3k2r/8/8/8/8/8/4r3/R3K2R w KQkq - 0 1");
	CHECK(board.inCheck());
	for (const auto& m : board.legalMoves())
	{
		CHECK(!m.isCastling);
	}

	printf("castling unit checks passed\n");
}

static void testEnPassant()
{
	printf("---- testing en passant ----\n");
	ChessLibBoard board;

	// Position: White pawn on e5, Black pawn just pushed d7-d5 (en passant square
	// d6)
	board.setFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
	auto moves = board.legalMovesFrom(Square{4, 4}); // e5

	bool foundEp = false;
	Move epMove;
	for (const auto& m : moves)
	{
		if (m.from == Square{4, 4} && m.to == Square{3, 5})
		{
			foundEp = true;
			epMove = m;
			break;
		}
	}
	CHECK(foundEp);
	CHECK(epMove.isEnPassant);
	CHECK(epMove.isCapture);
	CHECK(ChessLibBoard::toUci(epMove) == "e5d6");

	TacticInfo tInfo = TacticDetector::analyze(board, epMove);
	CHECK(tInfo.enPassant);
	CHECK(tInfo.capture);

	std::string san = AnnotationWriter::notation(epMove, "", true, false, false, false, PieceType::Queen);
	CHECK(san == "exd6");

	CHECK(board.makeMove(epMove));
	auto wPawn = board.pieceAt(Square{3, 5}); // d6
	CHECK(wPawn && wPawn->second == PieceType::Pawn && wPawn->first == Color::White);
	CHECK(!board.pieceAt(Square{4, 4})); // e5 is empty
	CHECK(!board.pieceAt(Square{3, 4})); // d5 captured pawn is gone

	// Test Undo
	board.undoMove();
	auto wPawnOrig = board.pieceAt(Square{4, 4});
	auto bPawnOrig = board.pieceAt(Square{3, 4});
	CHECK(wPawnOrig && wPawnOrig->second == PieceType::Pawn && wPawnOrig->first == Color::White);
	CHECK(bPawnOrig && bPawnOrig->second == PieceType::Pawn && bPawnOrig->first == Color::Black);
	CHECK(!board.pieceAt(Square{3, 5}));

	// Test Black En Passant
	board.setFen("4k3/8/8/8/3Pp3/8/8/4K3 b - d3 0 1");
	auto bMoves = board.legalMovesFrom(Square{4, 3}); // e4
	bool foundBEp = false;
	Move bEpMove;
	for (const auto& m : bMoves)
	{
		if (m.from == Square{4, 3} && m.to == Square{3, 2})
		{
			foundBEp = true;
			bEpMove = m;
			break;
		}
	}
	CHECK(foundBEp);
	CHECK(bEpMove.isEnPassant);
	CHECK(bEpMove.isCapture);
	CHECK(ChessLibBoard::toUci(bEpMove) == "e4d3");

	std::string bSan = AnnotationWriter::notation(bEpMove, "", true, false, false, false, PieceType::Queen);
	CHECK(bSan == "exd3");

	CHECK(board.makeMove(bEpMove));
	auto bPawn = board.pieceAt(Square{3, 2}); // d3
	CHECK(bPawn && bPawn->second == PieceType::Pawn && bPawn->first == Color::Black);
	CHECK(!board.pieceAt(Square{4, 3})); // e4 is empty
	CHECK(!board.pieceAt(Square{3, 3})); // d4 captured pawn is gone

	board.undoMove();
	CHECK(board.pieceAt(Square{4, 3}) && board.pieceAt(Square{4, 3})->second == PieceType::Pawn);
	CHECK(board.pieceAt(Square{3, 3}) && board.pieceAt(Square{3, 3})->second == PieceType::Pawn);
	CHECK(!board.pieceAt(Square{3, 2}));

	// Test fromUci resolution without explicit flags
	board.setFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
	auto parsed = ChessLibBoard::fromUci("e5d6");
	CHECK(parsed.has_value());
	CHECK(board.isLegal(*parsed));
	CHECK(board.makeMove(*parsed));
	CHECK(board.pieceAt(Square{3, 5}) && board.pieceAt(Square{3, 5})->second == PieceType::Pawn);
	CHECK(!board.pieceAt(Square{3, 4}));

	printf("en passant unit checks passed\n");
}

static void testPromotion()
{
	printf("---- testing promotion ----\n");
	ChessLibBoard board;

	// Position: White pawn on e7, Black king on a8
	board.setFen("k7/4P3/8/8/8/8/8/4K3 w - - 0 1");
	auto moves = board.legalMovesFrom(Square{4, 6}); // e7

	CHECK(moves.size() == 4);
	bool foundQ = false, foundR = false, foundB = false, foundN = false;
	Move moveQ, moveN;
	for (const auto& m : moves)
	{
		CHECK(m.isPromotion);
		if (m.promotion == PieceType::Queen)
		{
			foundQ = true;
			moveQ = m;
		}
		if (m.promotion == PieceType::Rook)
			foundR = true;
		if (m.promotion == PieceType::Bishop)
			foundB = true;
		if (m.promotion == PieceType::Knight)
		{
			foundN = true;
			moveN = m;
		}
	}
	CHECK(foundQ && foundR && foundB && foundN);

	// Test UCI string
	CHECK(ChessLibBoard::toUci(moveQ) == "e7e8q");
	CHECK(ChessLibBoard::toUci(moveN) == "e7e8n");

	// Test Annotation
	std::string sanQ = AnnotationWriter::notation(moveQ, "", false, false, false, true, PieceType::Queen);
	CHECK(sanQ == "e8=Q");
	std::string sanN = AnnotationWriter::notation(moveN, "", false, false, false, true, PieceType::Knight);
	CHECK(sanN == "e8=N");

	TacticInfo tInfo = TacticDetector::analyze(board, moveQ);
	CHECK(tInfo.promotion);

	// Test Queen Promotion Move
	CHECK(board.makeMove(moveQ));
	auto promotedQueen = board.pieceAt(Square{4, 7});
	CHECK(promotedQueen && promotedQueen->second == PieceType::Queen && promotedQueen->first == Color::White);
	CHECK(!board.pieceAt(Square{4, 6}));

	// Test Undo
	board.undoMove();
	auto restoredPawn = board.pieceAt(Square{4, 6});
	CHECK(restoredPawn && restoredPawn->second == PieceType::Pawn && restoredPawn->first == Color::White);
	CHECK(!board.pieceAt(Square{4, 7}));

	// Test Knight Promotion (Underpromotion)
	CHECK(board.makeMove(moveN));
	auto promotedKnight = board.pieceAt(Square{4, 7});
	CHECK(promotedKnight && promotedKnight->second == PieceType::Knight && promotedKnight->first == Color::White);

	// Test Promotion with Capture
	board.setFen("3rk3/4P3/8/8/8/8/8/4K3 w - - 0 1");
	auto capPromoMoves = board.legalMovesFrom(Square{4, 6});
	bool foundCapPromo = false;
	Move capPromoQ;
	for (const auto& m : capPromoMoves)
	{
		if (m.to == Square{3, 7} && m.isPromotion && m.promotion == PieceType::Queen)
		{
			foundCapPromo = true;
			capPromoQ = m;
			break;
		}
	}
	CHECK(foundCapPromo);
	CHECK(capPromoQ.isCapture);
	std::string sanCap = AnnotationWriter::notation(capPromoQ, "", true, false, false, true, PieceType::Queen);
	CHECK(sanCap == "exd8=Q");

	// Test Black Pawn Promotion
	board.setFen("4k3/8/8/8/8/8/4p3/K7 b - - 0 1");
	auto bMoves = board.legalMovesFrom(Square{4, 1}); // e2
	CHECK(bMoves.size() == 4);
	Move bPromoQ;
	for (const auto& m : bMoves)
	{
		if (m.promotion == PieceType::Queen)
			bPromoQ = m;
	}
	CHECK(ChessLibBoard::toUci(bPromoQ) == "e2e1q");
	CHECK(board.makeMove(bPromoQ));
	auto bQueen = board.pieceAt(Square{4, 0});
	CHECK(bQueen && bQueen->second == PieceType::Queen && bQueen->first == Color::Black);

	printf("promotion unit checks passed\n");
}

static void testTacticDetection()
{
	printf("---- testing tactic detection ----\n");
	ChessLibBoard board;

	// 1. Move 8 False Skewer check (Qg4+ vs King on d7, blocked/defended)
	board.setFen("r1bq1b1r/pp1kpppp/2n2n2/3pP1B1/2P5/N7/PP3PPP/R2QKBNR w - - 1 8");
	auto qCheck = ChessLibBoard::fromUci("d1g4");
	CHECK(qCheck.has_value());
	if (qCheck)
	{
		TacticInfo t = TacticDetector::analyze(board, *qCheck);
		CHECK(t.check);
		CHECK(!t.skewer); // Must NOT be flagged as a skewer
	}

	// 2. Move 12 cxd5 (Pawn takes Knight)
	board.setFen("r1b1k2r/pp2bppp/1qp1p3/3n4/2P5/N7/PP1B1PPP/R3KBNR w - - 0 12");
	auto cxd5 = ChessLibBoard::fromUci("c4d5");
	CHECK(cxd5.has_value());
	if (cxd5)
	{
		TacticInfo t = TacticDetector::analyze(board, *cxd5);
		CHECK(!t.hangsPiece); // Winning capture must NOT be flagged as hanging piece
	}

	// 3. True Skewer (Queen on h8 checks King on c8 with Rook on a8 behind)
	board.setFen("r1k5/8/8/8/8/8/8/4K2Q w - - 0 1");
	auto qh8 = ChessLibBoard::fromUci("h1h8");
	CHECK(qh8.has_value());
	if (qh8)
	{
		TacticInfo t = TacticDetector::analyze(board, *qh8);
		CHECK(t.check);
		CHECK(t.skewer); // True skewer along 8th rank
	}

	printf("tactic detection checks passed\n");
}

static void testMoveClassificationAndTrades()
{
	printf("---- testing classification and trade detection ----\n");
	NullAI nullAi;

	// 1. Bishop takes free Knight (Bxa3): NOT a trade, but a free material capture
	{
		AnnotationJob job;
		job.beforeFen = "r1b1k2r/3pbppp/1qp1p3/p2P4/8/N1P5/1P1B1PPP/R2QKB1R b - - 0 15";
		auto bxa3 = ChessLibBoard::fromUci("f8a3");
		CHECK(bxa3.has_value());
		job.move = *bxa3;
		job.mover = Color::Black;
		job.hasPrevMove = false;
		job.legalMoveCount = 30;
		job.fullMoveNumber = 15;

		ChessLibBoard testBoard;
		testBoard.setFen(job.beforeFen);
		testBoard.makeMove(job.move);
		job.afterFen = testBoard.getFen();

		MoveAnnotation ann = AnnotationPipeline::build(job, nullAi);
		CHECK(ann.hasCapture);
		CHECK(!ann.isTrade); // Free capture is NOT a trade
		CHECK(ann.tradeEvent.find("Routine piece trade") == std::string::npos);
		CHECK(ann.tradeEvent.find("MATERIAL:") != std::string::npos);
	}

	// 2. King Walk on move 4 (Kd7) with significant win chance drop: classified as Mistake/Blunder, NOT Inaccuracy
	{
		ClassificationInput in;
		in.evalBeforeCp = 60;
		in.evalAfterCp = -220; // loss of 280 cp
		in.bestMoveCp = 60;
		in.winChanceBefore = 0.66f;
		in.winChanceAfter = 0.44f;
		in.winChanceDelta = -0.22f;
		in.pieceMoved = PieceType::King;
		in.legalMoveCount = 25;
		in.openingPly = 4;
		MoveQuality q = classify(in);
		CHECK(q == MoveQuality::Mistake || q == MoveQuality::Blunder);
		CHECK(q != MoveQuality::Inaccuracy);
	}

	// 3. Decided position capture (Bxa6 when up +15.0): classified as Best/Great, NOT Miss
	{
		ClassificationInput in;
		in.evalBeforeCp = 1800;
		in.evalAfterCp = 1600; // 200cp loss due to mate depth variation, but still +16.0
		in.bestMoveCp = 1800;
		in.winChanceBefore = 1.0f;
		in.winChanceAfter = 1.0f;
		in.winChanceDelta = 0.0f;
		in.tactics.capture = true;
		in.move.isCapture = true;
		in.pieceMoved = PieceType::Bishop;
		in.legalMoveCount = 20;
		MoveQuality q = classify(in);
		CHECK(q == MoveQuality::Great || q == MoveQuality::Best || q == MoveQuality::Good);
		CHECK(q != MoveQuality::Miss);
		CHECK(q != MoveQuality::Blunder);
	}

	// 4. Mate-in-1: classified as Best/Great with CHECKMATE, NOT Brilliant
	{
		ClassificationInput in;
		in.evalBeforeCp = 800;
		in.evalAfterCp = 20000;
		in.bestMoveCp = 20000;
		in.winChanceBefore = 0.99f;
		in.winChanceAfter = 1.0f;
		in.tactics.checkmate = true;
		in.tactics.check = true;
		in.pieceMoved = PieceType::Queen;
		in.legalMoveCount = 15;
		MoveQuality q = classify(in);
		CHECK(q != MoveQuality::Brilliant);
		CHECK(q == MoveQuality::Best || q == MoveQuality::Great);
	}

	printf("classification and trade checks passed\n");
}

static void testTextWrapping()
{
	printf("---- testing text wrapping ----\n");

	// 1. 14 character wrapping (width 1280 resolution panel width)
	std::string text1 = "White (1) - Good e4 (+0.20)";
	auto lines1 = NarrativeRenderSystem::wrapLines(text1, 14);
	for (const auto& l : lines1)
	{
		CHECK(static_cast<int>(l.size()) <= 14);
	}
	CHECK(lines1.size() == 3);
	CHECK(lines1[0] == "White (1) -");
	CHECK(lines1[1] == "Good e4");
	CHECK(lines1[2] == "(+0.20)");

	// 2. 30 character wrapping (width 2560 resolution panel width)
	auto lines2 = NarrativeRenderSystem::wrapLines(text1, 30);
	CHECK(lines2.size() == 1);
	CHECK(lines2[0] == text1);

	// 3. Long word without spaces
	std::string longWord = "Supercalifragilistic";
	auto lines3 = NarrativeRenderSystem::wrapLines(longWord, 10);
	CHECK(lines3.size() == 2);
	CHECK(lines3[0] == "Supercalif");
	CHECK(lines3[1] == "ragilistic");

	// 4. Message spacing: wrapped lines of the same message stay adjacent,
	// while different messages have an extra blank line between them.
	std::vector<std::string> chunks = {"White (1) - Good e4 (+0.20)", "Black (1) - Best e5 (0.00)"};
	auto storyLines = NarrativeRenderSystem::formatStoryLines(chunks, 12);
	// Message 1 wraps into 3 lines, then 1 blank line separator, then Message 2 wraps into 3 lines
	CHECK(storyLines.size() == 7);
	CHECK(storyLines[0] == "White (1) -");
	CHECK(storyLines[1] == "Good e4");
	CHECK(storyLines[2] == "(+0.20)");
	CHECK(storyLines[3] == "");
	CHECK(storyLines[4] == "Black (1) -");
	CHECK(storyLines[5] == "Best e5");
	CHECK(storyLines[6] == "(0.00)");

	// 5. Typewriter char-by-char line builder
	{
		std::vector<std::string> lines = {"Hello", "world", "", "Chess"};
		size_t total = NarrativeRenderSystem::countTotalChars(lines);
		CHECK(total == 15); // 5 + 5 + 0 + 5

		auto d0 = NarrativeRenderSystem::buildTypewriterLines(lines, 0);
		CHECK(d0.empty());

		auto d3 = NarrativeRenderSystem::buildTypewriterLines(lines, 3);
		CHECK(d3.size() == 1);
		CHECK(d3[0] == "Hel");

		auto d5 = NarrativeRenderSystem::buildTypewriterLines(lines, 5);
		CHECK(d5.size() == 1);
		CHECK(d5[0] == "Hello");

		auto d8 = NarrativeRenderSystem::buildTypewriterLines(lines, 8);
		CHECK(d8.size() == 2);
		CHECK(d8[0] == "Hello");
		CHECK(d8[1] == "wor");

		auto d10 = NarrativeRenderSystem::buildTypewriterLines(lines, 10);
		CHECK(d10.size() == 2);
		CHECK(d10[0] == "Hello");
		CHECK(d10[1] == "world");

		auto d11 = NarrativeRenderSystem::buildTypewriterLines(lines, 11);
		CHECK(d11.size() == 4);
		CHECK(d11[0] == "Hello");
		CHECK(d11[1] == "world");
		CHECK(d11[2] == "");
		CHECK(d11[3] == "C");

		auto d15 = NarrativeRenderSystem::buildTypewriterLines(lines, 15);
		CHECK(d15.size() == 4);
		CHECK(d15[0] == "Hello");
		CHECK(d15[1] == "world");
		CHECK(d15[2] == "");
		CHECK(d15[3] == "Chess");

		auto d20 = NarrativeRenderSystem::buildTypewriterLines(lines, 20);
		CHECK(d20.size() == 4);
		CHECK(d20 == lines);
	}

	printf("text wrapping and typewriter checks passed\n");
}

static void testGameIntensity()
{
	printf("---- testing game intensity calculation ----\n");

	// 1. Starting position: calm, intensity near 0.0
	{
		ChessState state;
		state.board = std::make_shared<ChessLibBoard>();
		state.board->loadStartPosition();
		float intensity = AnimationSystem::computeGameIntensity(state);
		CHECK(intensity >= 0.0f && intensity <= 0.05f);
	}

	// 2. Position with King in Check: should have significant check bonus
	{
		ChessState state;
		state.board = std::make_shared<ChessLibBoard>();
		// Black king on e8 in check from White Queen on e7
		state.board->setFen("4k3/4Q3/8/8/8/8/8/4K3 b - - 0 1");
		float intensity = AnimationSystem::computeGameIntensity(state);
		CHECK(intensity >= 0.25f);
	}

	// 3. Checkmate position: maximum intensity 1.0
	{
		ChessState state;
		state.board = std::make_shared<ChessLibBoard>();
		// Fool's mate: Black checkmated
		state.board->setFen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
		CHECK(state.board->isGameOver());
		CHECK(state.board->gameState() == GameState::Checkmate);
		float intensity = AnimationSystem::computeGameIntensity(state);
		CHECK(intensity == 1.0f);
	}

	printf("game intensity checks passed\n");
}

static void testNarratorContext()
{
	printf("---- testing Narrator context & premise ----\n");

	// 1. PassThroughNarrator custom premise and history
	{
		PassThroughNarrator narrator;
		narrator.setPremise("Two rival space captains met in orbit.");
		CHECK(narrator.getPremise() == "Two rival space captains met in orbit.");

		StoryStream stream;
		narrator.narrateIntro(stream);
		auto history = narrator.getStoryHistory();
		CHECK(history.size() == 1);
		CHECK(history[0] == "Two rival space captains met in orbit.");

		narrator.reset();
		CHECK(narrator.getStoryHistory().empty());
	}

	// 2. LlamaNarrator premise, memory, and sanitization
	{
		LlamaNarrator llama;
		llama.setPremise("Commander Rowan and Warlord Vane clashed across the valley.");
		CHECK(llama.getPremise() == "Commander Rowan and Warlord Vane clashed across the valley.");

		StoryStream stream;
		llama.narrateIntro(stream);
		auto history = llama.getStoryHistory();
		CHECK(!history.empty());
		CHECK(history[0].find("Commander Rowan") != std::string::npos);

		llama.reset();
		CHECK(llama.getStoryHistory().empty());
	}

	// 3. Font sanitization checks (smart quotes, apostrophes, dashes, hashes)
	{
		std::string messy = "Story: It's Rowan's “secret” plan—an ambush… #1!";
		std::string cleaned = LlamaNarrator::sanitizeForEngine(messy);
		CHECK(cleaned.find('#') == std::string::npos);
		CHECK(cleaned.find('\'') == std::string::npos);
		CHECK(cleaned.find("Story:") == std::string::npos);
		CHECK(cleaned.find("\"secret\"") != std::string::npos);
		CHECK(cleaned.find("-") != std::string::npos);
	}

	// 4. Special tokens and casing normalization checks
	{
		std::string tokenJunk = "<end_of_turn>.";
		std::string cleaned = LlamaNarrator::cleanNarrativeText(tokenJunk, "Raider Max", "Warlord Stryker");
		CHECK(cleaned.empty());

		std::string tokenJunk2 = "</start_of_turn>";
		CHECK(LlamaNarrator::cleanNarrativeText(tokenJunk2).empty());

		std::string lowercaseStory =
			"raider max's light riders darted into an open post against warlord stryker's perimeter";
		std::string fixed = LlamaNarrator::cleanNarrativeText(lowercaseStory, "Raider Max", "Warlord Stryker");
		CHECK(fixed.find("Raider Max") != std::string::npos);
		CHECK(fixed.find("Warlord Stryker") != std::string::npos);
		CHECK(fixed[0] == 'R');
		CHECK(fixed.back() == '.');

		std::string embeddedTokens = "Raider Max advanced across the dunes.<end_of_turn>";
		std::string cleanedEmbedded =
			LlamaNarrator::cleanNarrativeText(embeddedTokens, "Raider Max", "Warlord Stryker");
		CHECK(cleanedEmbedded.find("<end_of_turn>") == std::string::npos);
		CHECK(cleanedEmbedded == "Raider Max advanced across the dunes.");
	}

	printf("narrator context checks passed\n");
}

static void testNarrativeVerification()
{
	printf("---- testing Narrative Verification & Endgame Acceptance Criteria ----\n");

	LlamaNarrator llama;
	std::string modelPath = "assets/models/gemma-2-2b-it-Q4_K_M.gguf";
	bool loaded = llama.load(modelPath);
	if (!loaded)
	{
		// Try fallback relative path
		modelPath = "../assets/models/gemma-2-2b-it-Q4_K_M.gguf";
		loaded = llama.load(modelPath);
	}
	CHECK(loaded);
	printf("Loaded GGUF model for narrative verification: %s\n", modelPath.c_str());

	llama.setLeaders("Admiral Drake", "Captain Flint");
	llama.setActivePremise("Admiral Drake and Captain Flint clashed in stormy waters.");

	// 1. Verify sliding context window (Move 20+ contains only sliding window of last 4 turns)
	for (int i = 1; i <= 25; ++i)
	{
		llama.addStoryBeat("Sentence for turn " + std::to_string(i) + " of the naval battle.");
	}

	MoveAnnotation ann;
	ann.mover = Color::Black;
	ann.fullMoveNumber = 26;
	ann.whitePieces = 8;
	ann.blackPieces = 1; // Lone king for Flint
	ann.pieceMoved = PieceType::King;
	ann.quality = MoveQuality::Good;

	std::string prompt = llama.testBuildContextPrompt("- Drake: advanced heavy ships.\n- Flint: retreated.", 50, ann);
	printf("\n[Prompt Verification for Move 26]:\n%s\n", prompt.c_str());

	// Must include Opening Premise
	CHECK(prompt.find("Admiral Drake and Captain Flint clashed in stormy waters.") != std::string::npos);
	// Must include the last 4 turns: 25, 24, 23, 22
	CHECK(prompt.find("Sentence for turn 25") != std::string::npos);
	CHECK(prompt.find("Sentence for turn 24") != std::string::npos);
	CHECK(prompt.find("Sentence for turn 23") != std::string::npos);
	CHECK(prompt.find("Sentence for turn 22") != std::string::npos);
	// Must NOT include older turns (e.g. 1, 2, 10, 15, 20, 21)
	CHECK(prompt.find("Sentence for turn 1 ") == std::string::npos);
	CHECK(prompt.find("Sentence for turn 2 ") == std::string::npos);
	CHECK(prompt.find("Sentence for turn 10 ") == std::string::npos);
	CHECK(prompt.find("Sentence for turn 15 ") == std::string::npos);
	CHECK(prompt.find("Sentence for turn 20 ") == std::string::npos);
	CHECK(prompt.find("Sentence for turn 21 ") == std::string::npos);
	printf(">> Sliding window of 4 recent turns strictly verified (turns 1-21 omitted).\n");

	// 2. Verify low-material / lone King constraints in prompt and action descriptions
	CHECK(prompt.find("Captain Flint is severely depleted, fighting alone for survival") != std::string::npos);
	CHECK(prompt.find("Do NOT use army-scale descriptors for Captain Flint like 'disciplined lines', 'formations', 'reserves', or 'contingents'") != std::string::npos);

	std::string pieceAction = LlamaNarrator::testDescribePieceAction(ann, "Captain Flint", "Admiral Drake");
	printf("[Lone King Action Translation]: Flint %s\n", pieceAction.c_str());
	CHECK(pieceAction.find("formation") == std::string::npos);
	CHECK(pieceAction.find("reserves") == std::string::npos);
	CHECK(pieceAction.find("contingents") == std::string::npos);
	CHECK(pieceAction.find("disciplined lines") == std::string::npos);
	CHECK(pieceAction.find("outer barriers") == std::string::npos);
	printf(">> Lone King action descriptors strictly verified (army terms suppressed).\n");

	// 3. Verify dedicated Epilogue Prompt for Checkmate & Stalemate
	MoveAnnotation checkmateAnn = ann;
	checkmateAnn.mover = Color::White;
	checkmateAnn.tactics.checkmate = true;
	checkmateAnn.gameEnded = true;
	checkmateAnn.gameState = GameState::Checkmate;

	std::string mateEpiloguePrompt = llama.testBuildEpiloguePrompt("Drake struck the final blow.", 90, checkmateAnn);
	printf("\n[Checkmate Epilogue Prompt]:\n%s\n", mateEpiloguePrompt.c_str());
	CHECK(mateEpiloguePrompt.find("Admiral Drake has delivered the final blow and won the match.") != std::string::npos);
	CHECK(mateEpiloguePrompt.find("Write 1-2 vivid, dramatic concluding sentences providing thematic closure") != std::string::npos);

	MoveAnnotation stalemateAnn = ann;
	stalemateAnn.mover = Color::White;
	stalemateAnn.tactics.stalemate = true;
	stalemateAnn.gameEnded = true;
	stalemateAnn.gameState = GameState::Stalemate;

	std::string staleEpiloguePrompt = llama.testBuildEpiloguePrompt("Drake boxed Flint in.", 90, stalemateAnn);
	printf("\n[Stalemate Epilogue Prompt]:\n%s\n", staleEpiloguePrompt.c_str());
	CHECK(staleEpiloguePrompt.find("ending in an unresolved, bitter stalemate") != std::string::npos);
	CHECK(staleEpiloguePrompt.find("Write 1-2 vivid, dramatic concluding sentences providing thematic closure") != std::string::npos);
	printf(">> Epilogue prompts for Checkmate and Stalemate verified.\n");

	// 4. Live execution: Run live turns and verify no consecutive duplicate sentences & epilogue generation
	llama.reset();
	llama.setPremise("Admiral Drake vs Captain Flint on stormy Caribbean waters");
	StoryStream stream;
	llama.narrateIntro(stream);

	std::vector<std::string> generatedSentences;
	auto initialHistory = llama.getStoryHistory();
	for (const auto& s : initialHistory) generatedSentences.push_back(s);

	// Simulate 6 live turns with Llama model inference
	struct TurnMock {
		PieceType piece;
		Color mover;
		bool check;
		bool checkmate;
		bool gameEnded;
		GameState gameState;
		int wPieces;
		int bPieces;
		MoveQuality quality;
	};

	std::vector<TurnMock> turns = {
		{PieceType::Pawn, Color::White, false, false, false, GameState::Ongoing, 16, 16, MoveQuality::Best},
		{PieceType::Pawn, Color::Black, false, false, false, GameState::Ongoing, 16, 16, MoveQuality::Good},
		{PieceType::Knight, Color::White, false, false, false, GameState::Ongoing, 16, 16, MoveQuality::Best},
		{PieceType::Knight, Color::Black, false, false, false, GameState::Ongoing, 16, 16, MoveQuality::Good},
		{PieceType::Queen, Color::White, true, false, false, GameState::Ongoing, 16, 1, MoveQuality::Great},
		{PieceType::Queen, Color::White, false, true, true, GameState::Checkmate, 16, 1, MoveQuality::Best}
	};

	int moveNum = 1;
	for (const auto& tm : turns)
	{
		MoveAnnotation mAnn;
		mAnn.mover = tm.mover;
		mAnn.pieceMoved = tm.piece;
		mAnn.fullMoveNumber = moveNum++;
		mAnn.tactics.check = tm.check;
		mAnn.tactics.checkmate = tm.checkmate;
		mAnn.gameEnded = tm.gameEnded;
		mAnn.gameState = tm.gameState;
		mAnn.whitePieces = tm.wPieces;
		mAnn.blackPieces = tm.bPieces;
		mAnn.quality = tm.quality;
		mAnn.san = (tm.mover == Color::White ? "e4" : "e5");

		llama.narrate(mAnn, stream);
	}

	auto finalHistory = llama.getStoryHistory();
	printf("\n==================== MATCH STORY CHRONICLE ====================\n");
	for (size_t i = 0; i < finalHistory.size(); ++i)
	{
		printf("[%zu] %s\n", i, finalHistory[i].c_str());
		if (i > 0)
		{
			// Verify no consecutive duplicate sentences
			CHECK(finalHistory[i] != finalHistory[i - 1]);
		}
	}
	printf("===============================================================\n");

	// Verify epilogue is generated and captured in final story history
	CHECK(finalHistory.size() >= 3);
	std::string lastSentence = finalHistory.back();
	CHECK(!lastSentence.empty());
	printf(">> Live story generated %zu sentences without duplicates. Epilogue produced: \"%s\"\n",
		   finalHistory.size(), lastSentence.c_str());
}

int main(int argc, char** argv)
{
	testMinimaxAI();
	testCastling();
	testEnPassant();
	testPromotion();
	testTacticDetection();
	testMoveClassificationAndTrades();
	testTextWrapping();
	testGameIntensity();
	testNarratorContext();
	testNarrativeVerification();

	ChessState state;
	state.board = std::make_shared<ChessLibBoard>();
	state.board->loadStartPosition();

	std::string sfPath = argc > 1 ? argv[1] : "bin/stockfish";
	{
		auto sf = std::make_shared<StockfishUCIAI>(sfPath);
		if (sf->isAvailable())
		{
			sf->setStrength(ChessConfig::DEFAULT_SKILL, ChessConfig::DEFAULT_ELO);
			state.ai = sf;
			printf("AI: stockfish (%s)\n", sfPath.c_str());
		}
	}
	if (!state.ai)
	{
		printf("AI: MinimaxAI (in-process fallback, no stockfish binary at %s)\n", sfPath.c_str());
		auto minimax = std::make_shared<MinimaxAI>();
		minimax->setStrength(ChessConfig::DEFAULT_SKILL, ChessConfig::DEFAULT_ELO);
		state.ai = minimax;
	}

	state.narratorImpl = std::make_shared<PassThroughNarrator>();
	state.narrator = std::make_shared<NarratorThread>(state.narratorImpl);
	state.narrator->start();

	// ---- play until the game ends ----
	// The annotation pipeline now runs on the AsyncAnnotator worker thread,
	// exactly like in the game: the move is applied instantly, the annotation
	// is polled once ready.
	state.annotator = std::make_shared<AsyncAnnotator>(state.ai);
	state.annotator->start();

	int guard = 0;
	int annotations = 0;
	while (!state.board->isGameOver() && guard++ < 300)
	{
		Color mover = state.board->sideToMove();
		state.ai->setPosition(state.board->getFen());
		Move move = state.ai->bestMove(state.board->legalMoves());

		int legalBefore = static_cast<int>(state.board->legalMoves().size());
		std::string beforeFen = state.board->getFen();
		CHECK(state.board->makeMove(move));
		std::string afterFen = state.board->getFen();

		AnnotationJob job;
		job.beforeFen = beforeFen;
		job.afterFen = afterFen;
		job.move = move;
		job.mover = mover;
		job.prevEvalValid = state.hasLastAnnotation;
		job.prevEval = state.lastAnnotation.evalAfterCp;
		job.legalMoveCount = legalBefore;
		job.fullMoveNumber = state.board->fullMoveNumber();
		job.isGameOver = state.board->isGameOver();
		job.gameState = state.board->gameState();
		CHECK(state.annotator->submit(job));
		state.moveAppliedPendingAnnotation = true;

		// Wait for the worker (mirrors the game's per-frame poll).
		MoveAnnotation ann;
		{
			int spins = 0;
			while (!state.annotator->poll(ann) && spins++ < 2000)
			{
				using namespace std::chrono_literals;
				std::this_thread::sleep_for(5ms);
			}
			CHECK(spins < 2000);
		}
		state.moveAppliedPendingAnnotation = false;
		state.lastAnnotation = ann;
		state.hasLastAnnotation = true;
		AnnotationSystem::publish(state, ann);
		++annotations;

		if (guard <= 8)
		{
			printf("ply %2d: %s %-8s %-14s loss %5.2f  eval %+.2f\n", guard, mover == Color::White ? "W" : "B",
				   ann.san.c_str(), qualityName(ann.quality), static_cast<float>(ann.lossCp) / 100.0f,
				   static_cast<float>(ann.evalAfterCp) / 100.0f);
		}
	}

	state.annotator->stop();
	state.annotator.reset();

	printf("game ended after %d plies: %s\n", guard,
		   state.board->isGameOver() ? (state.board->gameState() == GameState::Checkmate
											? "checkmate"
											: (state.board->gameState() == GameState::Stalemate ? "stalemate" : "draw"))
									 : "not finished (guard)");
	CHECK(annotations > 0);
	CHECK(!state.moveLog.empty());
	CHECK(state.board->isGameOver() || guard >= 300);

	// ---- let the narrator worker finish all queued items ----
	{
		int spins = 0;
		while ((state.narrator->busy() || state.narrator->stream()->status() == StoryStatus::Generating) &&
			   spins++ < 300)
		{
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(10ms);
		}
	}
	auto lines = state.narrator->stream()->drain();
	StoryStatus st = state.narrator->stream()->status();
	printf("story lines drained: %zu, final status=%d\n", lines.size(), static_cast<int>(st));
	CHECK(!lines.empty());
	// A blunder or mate during the game must end the story abruptly.
	CHECK(st == StoryStatus::EndedAbruptly || st == StoryStatus::EndedNaturally);

	// Sanity on a few annotation strings (must respect the font charset:
	// no '#', no apostrophes).
	for (const auto& line : lines)
	{
		CHECK(line.find('#') == std::string::npos);
		CHECK(line.find('\'') == std::string::npos);
	}

	state.narrator->stop();
	state.narrator.reset();
	state.ai->shutdown();

	if (failures == 0)
		printf("ALL INTEGRATION CHECKS PASSED\n");
	else
		printf("%d CHECK(S) FAILED\n", failures);
	return failures ? 1 : 0;
}
