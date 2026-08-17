#pragma once

// Stage-2 narrator: uses llama.cpp to generate dramatic story text based on
// move annotations. Runs strictly on the NarratorThread worker.
// Implements Option C cadence (batches White + Black moves, or triggers
// immediately on critical blunders/checkmates) in a stateless single-turn manner.

#include "chess/AnnotationWriter.h"
#include "chess/ChessTypes.h"
#include "llama.h"
#include "narrator/INarrator.h"
#include "narrator/StoryStream.h"
#include <weird-engine/Logger.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace wchess
{
	class LlamaNarrator : public INarrator
	{
	public:
		LlamaNarrator() = default;

		~LlamaNarrator() override
		{
			unload();
		}

		std::string name() const override
		{
			return "llama";
		}

		void cancel() override
		{
			m_cancel.store(true);
		}

		void reset() override
		{
			m_bufferedWhite.reset();
			m_cancel.store(false);
		}

		bool load(const std::string& modelPath)
		{
			if (modelPath.empty())
			{
				WeirdEngine::Logger::error("[LlamaNarrator] Error: Model path is empty.");
				return false;
			}

			std::error_code ec;
			if (!std::filesystem::exists(modelPath, ec))
			{
				WeirdEngine::Logger::error("[LlamaNarrator] Error: File does not exist: " + modelPath +
										   " (error: " + ec.message() + ")");
				return false;
			}

			auto fileSize = std::filesystem::file_size(modelPath, ec);
			WeirdEngine::Logger::log("[LlamaNarrator] Attempting to load GGUF model: " + modelPath +
									 " (size: " + std::to_string(fileSize / (1024 * 1024)) + " MB)");

			unload();
			m_cancel.store(false);

			llama_backend_init();

			llama_model_params modelParams = llama_model_default_params();
			modelParams.n_gpu_layers = 0; // pure CPU for maximum compatibility
			m_model = llama_model_load_from_file(modelPath.c_str(), modelParams);
			if (!m_model)
			{
				WeirdEngine::Logger::error("[LlamaNarrator] Error: llama_model_load_from_file failed for: " + modelPath +
										   ". (Corrupted GGUF file or incompatible quantization format)");
				return false;
			}

			m_vocab = llama_model_get_vocab(m_model);
			if (!m_vocab)
			{
				WeirdEngine::Logger::error("[LlamaNarrator] Error: Failed to retrieve vocabulary from model.");
				llama_model_free(m_model);
				m_model = nullptr;
				return false;
			}

			llama_context_params ctxParams = llama_context_default_params();
			int32_t trainCtx = llama_model_n_ctx_train(m_model);
			ctxParams.n_ctx = (trainCtx > 0) ? std::min<uint32_t>(512, static_cast<uint32_t>(trainCtx)) : 512;
			ctxParams.n_threads = 4;
			ctxParams.no_perf = true;

			m_ctx = llama_init_from_model(m_model, ctxParams);
			if (!m_ctx)
			{
				WeirdEngine::Logger::error("[LlamaNarrator] Error: llama_init_from_model failed (insufficient memory or context params error).");
				llama_model_free(m_model);
				m_model = nullptr;
				return false;
			}

			// Initialize sampling chain
			llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
			m_sampler = llama_sampler_chain_init(sparams);
			llama_sampler_chain_add(m_sampler, llama_sampler_init_top_p(0.90f, 1));
			llama_sampler_chain_add(m_sampler, llama_sampler_init_temp(0.75f));
			llama_sampler_chain_add(m_sampler, llama_sampler_init_dist(42));

			m_loadedPath = modelPath;
			WeirdEngine::Logger::log("[LlamaNarrator] Successfully initialized GGUF model: " + modelPath);
			return true;
		}

		bool isLoaded() const
		{
			return m_model != nullptr && m_ctx != nullptr;
		}

		void narrate(const MoveAnnotation& annotation, StoryStream& out) override
		{
			if (!isLoaded())
				return;

			// Option C cadence:
			// If White makes a normal (non-game-ending / non-critical) move,
			// buffer it and wait for Black's move so both sides form a cohesive turn story.
			bool isCritical = annotation.tactics.checkmate || annotation.gameEnded ||
							  (annotation.quality == MoveQuality::Blunder && annotation.impact == ImpactLevel::Critical);

			if (annotation.mover == Color::White && !isCritical)
			{
				m_bufferedWhite = annotation;
				return;
			}

			// Construct input prompt for the current turn
			std::string summary;
			if (m_bufferedWhite.has_value())
			{
				summary = "White played " + m_bufferedWhite->san + " (" + m_bufferedWhite->title + "). " +
						  "Black played " + annotation.san + " (" + annotation.title + ").";
				if (!annotation.specialEvent.empty())
					summary += " Event: " + annotation.specialEvent + ".";
				if (!annotation.gameStatus.empty())
					summary += " Situation: " + annotation.gameStatus + ".";
				m_bufferedWhite.reset();
			}
			else
			{
				const std::string mover = annotation.mover == Color::White ? "White" : "Black";
				summary = mover + " played " + annotation.san + " (" + annotation.title + ").";
				if (!annotation.specialEvent.empty())
					summary += " Event: " + annotation.specialEvent + ".";
				if (!annotation.gameStatus.empty())
					summary += " Situation: " + annotation.gameStatus + ".";
			}

			std::string prompt =
				"You are a serialized fiction narrator. Map these chess events into a single dramatic sentence "
				"about human conflict, risk, victory or defeat. Do NOT use any chess words (no king, queen, bishop, "
				"knight, rook, pawn, board, check, checkmate, square).\n"
				"Event: " +
				summary + "\nStory:";

			WeirdEngine::Logger::log("[LlamaNarrator] Turn summary prompt: " + summary);
			out.setStatus(StoryStatus::Generating);

			std::string generatedText = generateStateless(prompt, 60);

			if (!generatedText.empty())
			{
				std::string clean = sanitizeForEngine(generatedText);
				if (!clean.empty())
				{
					WeirdEngine::Logger::log("[LlamaNarrator] Generated text: \"" + clean + "\"");
					out.append(clean);
				}
				else
				{
					WeirdEngine::Logger::warning("[LlamaNarrator] Generated text was empty or filtered out by font sanitizer.");
				}
			}
			else
			{
				WeirdEngine::Logger::warning("[LlamaNarrator] Model produced no tokens.");
			}

			if (annotation.tactics.checkmate || (annotation.gameEnded && annotation.gameState == GameState::Checkmate))
			{
				out.setStatus(StoryStatus::EndedAbruptly);
			}
			else if (annotation.quality == MoveQuality::Blunder && annotation.impact == ImpactLevel::Critical)
			{
				out.setStatus(StoryStatus::EndedAbruptly);
			}
			else if (annotation.gameEnded)
			{
				out.setStatus(StoryStatus::EndedNaturally);
			}
			else
			{
				out.setStatus(StoryStatus::Generating);
			}
		}

	private:
		std::string generateStateless(const std::string& prompt, int maxTokens)
		{
			if (!m_model || !m_ctx || !m_vocab || !m_sampler)
				return "";

			m_cancel.store(false);
			llama_kv_cache_clear(m_ctx);

			std::vector<llama_token> tokens(prompt.size() + 16);
			int n_tokens = llama_tokenize(m_vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()), tokens.data(),
										  static_cast<int32_t>(tokens.size()), true, false);
			if (n_tokens <= 0)
			{
				tokens.resize(static_cast<size_t>(-n_tokens));
				n_tokens = llama_tokenize(m_vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()), tokens.data(),
										  static_cast<int32_t>(tokens.size()), true, false);
			}
			if (n_tokens <= 0)
				return "";

			tokens.resize(static_cast<size_t>(n_tokens));

			llama_batch batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));
			if (llama_decode(m_ctx, batch) != 0)
			{
				return "";
			}

			std::string result;
			char pieceBuf[128];

			for (int i = 0; i < maxTokens; ++i)
			{
				if (m_cancel.load())
					break;

				llama_token token = llama_sampler_sample(m_sampler, m_ctx, -1);
				llama_sampler_accept(m_sampler, token);

				if (llama_vocab_is_eog(m_vocab, token))
					break;

				int n_piece = llama_token_to_piece(m_vocab, token, pieceBuf, sizeof(pieceBuf), 0, false);
				if (n_piece > 0)
				{
					std::string piece(pieceBuf, static_cast<size_t>(n_piece));
					// Stop at newline if it finishes the sentence
					if (piece.find('\n') != std::string::npos && !result.empty())
						break;
					result += piece;
				}

				llama_token nextTokens[1] = {token};
				llama_batch nextBatch = llama_batch_get_one(nextTokens, 1);
				if (llama_decode(m_ctx, nextBatch) != 0)
					break;
			}

			return result;
		}

		// Sanitizes text to only contain characters supported by WeirdEngine's SDF font
		// (A-Z, a-z, 0-9 and !"&_*()-=+?|.,:;). Apostrophes and non-supported symbols are stripped.
		static std::string sanitizeForEngine(const std::string& raw)
		{
			std::string out;
			out.reserve(raw.size());
			for (char c : raw)
			{
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
				{
					out += c;
				}
				else if (c == ' ' || c == '\n')
				{
					out += c;
				}
				else if (c == '!' || c == '"' || c == '&' || c == '_' || c == '*' || c == '(' || c == ')' ||
						 c == '-' || c == '=' || c == '+' || c == '?' || c == '|' || c == '.' || c == ',' ||
						 c == ':' || c == ';')
				{
					out += c;
				}
				// Skip apostrophes, #, non-ASCII/unicode characters per engine rules
			}

			// Trim leading/trailing whitespace
			size_t start = out.find_first_not_of(" \t\r\n");
			if (start == std::string::npos)
				return "";
			size_t end = out.find_last_not_of(" \t\r\n");
			return out.substr(start, end - start + 1);
		}

		void unload()
		{
			m_cancel.store(true);
			if (m_sampler)
			{
				llama_sampler_free(m_sampler);
				m_sampler = nullptr;
			}
			if (m_ctx)
			{
				llama_free(m_ctx);
				m_ctx = nullptr;
			}
			if (m_model)
			{
				llama_model_free(m_model);
				m_model = nullptr;
			}
			m_vocab = nullptr;
			m_loadedPath.clear();
		}

		llama_model* m_model = nullptr;
		const llama_vocab* m_vocab = nullptr;
		llama_context* m_ctx = nullptr;
		llama_sampler* m_sampler = nullptr;
		std::string m_loadedPath;

		std::optional<MoveAnnotation> m_bufferedWhite;
		std::atomic<bool> m_cancel{false};
	};
} // namespace wchess
