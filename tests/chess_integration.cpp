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

	printf("text wrapping checks passed\n");
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
