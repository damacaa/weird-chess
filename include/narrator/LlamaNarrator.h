#pragma once

// Stage-2 narrator: uses llama.cpp to generate dramatic serialized fiction based on
// move annotations, maintaining persistent context across all turns and messages.
// Runs strictly on the NarratorThread worker.

#include "chess/AnnotationWriter.h"
#include "chess/ChessTypes.h"
#include "config.h"
#include "llama.h"
#include "narrator/INarrator.h"
#include "narrator/StoryStream.h"
#include <weird-engine/Logger.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
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

		void setPremise(const std::string& premise) override
		{
			m_configuredPremise = premise;
		}

		std::string getPremise() const override
		{
			return m_activePremise.empty() ? m_configuredPremise : m_activePremise;
		}

		void setSeed(int64_t seed) override
		{
			m_configuredSeed = seed;
			if (m_model != nullptr && m_ctx != nullptr)
			{
				initSampler();
			}
		}

		int64_t getSeed() const override
		{
			return m_configuredSeed;
		}

		std::vector<std::string> getStoryHistory() const override
		{
			std::vector<std::string> fullHistory;
			if (!m_activePremise.empty())
				fullHistory.push_back(m_activePremise);
			fullHistory.insert(fullHistory.end(), m_storyHistory.begin(), m_storyHistory.end());
			return fullHistory;
		}

		void setDevice(const std::string& device, int gpuLayers = 99)
		{
			m_device = device;
			std::transform(m_device.begin(), m_device.end(), m_device.begin(), ::tolower);
			m_gpuLayers = std::max(0, gpuLayers);
		}

		std::string getDevice() const
		{
			return m_device;
		}

		int getGpuLayers() const
		{
			return m_gpuLayers;
		}

		void setThreadCount(int threads)
		{
			m_threadCount = std::max(1, threads);
		}

		int getThreadCount() const
		{
			return m_threadCount;
		}

		void reset() override
		{
			m_bufferedWhite.reset();
			m_cancel.store(false);
			m_storyHistory.clear();
			m_activePremise.clear();
			m_cachedPromptTokens.clear();
			m_turnIndex = 0;
			if (m_model != nullptr && m_ctx != nullptr)
			{
				initSampler(); // draws a fresh new random seed if m_configuredSeed < 0, or enforces configured seed
			}
			if (m_ctx)
			{
				llama_kv_cache_clear(m_ctx);
			}
		}

		// Sanitizes text to only contain characters supported by WeirdEngine's SDF font
		// (A-Z, a-z, 0-9 and !"&_*()-=+?|.,:;). Apostrophes and non-supported symbols are converted or stripped.
		static std::string sanitizeForEngine(const std::string& raw, bool isFullSentence = true)
		{
			std::string text = raw;

			// Replace UTF-8 multi-byte punctuation before single-byte filtering
			auto replaceAll = [](std::string& str, const std::string& from, const std::string& to)
			{
				size_t startPos = 0;
				while ((startPos = str.find(from, startPos)) != std::string::npos)
				{
					str.replace(startPos, from.length(), to);
					startPos += to.length();
				}
			};

			replaceAll(text, "\xE2\x80\x98", "");	 // ‘
			replaceAll(text, "\xE2\x80\x99", "");	 // ’
			replaceAll(text, "\xE2\x80\x9C", "\"");	 // “
			replaceAll(text, "\xE2\x80\x9D", "\"");	 // ”
			replaceAll(text, "\xE2\x80\x93", "-");	 // en-dash
			replaceAll(text, "\xE2\x80\x94", "-");	 // em-dash
			replaceAll(text, "\xE2\x80\xA6", "..."); // …
			replaceAll(text, "'", "");				 // strip standard apostrophes per engine charset rule

			std::string out;
			out.reserve(text.size());
			for (char c : text)
			{
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
				{
					out += c;
				}
				else if (c == ' ' || c == '\n')
				{
					out += c;
				}
				else if (c == '!' || c == '"' || c == '&' || c == '_' || c == '*' || c == '(' || c == ')' || c == '-' ||
						 c == '=' || c == '+' || c == '?' || c == '|' || c == '.' || c == ',' || c == ':' || c == ';')
				{
					out += c;
				}
			}

			if (!isFullSentence)
			{
				// For incremental streaming token pieces, preserve all spaces and leading/trailing whitespace
				return out;
			}

			// Remove leading hallucinated prefix tags
			const std::vector<std::string> prefixes = {"Story:",	 "Story :",	   "Continuation:", "Narrator:",
													   "Paragraph:", "Assistant:", "assistant:",	"Response:"};
			for (const auto& prefix : prefixes)
			{
				size_t p = out.find(prefix);
				if (p == 0)
				{
					out = out.substr(prefix.size());
				}
			}

			// Trim leading/trailing whitespace
			size_t start = out.find_first_not_of(" \t\r\n");
			if (start == std::string::npos)
				return "";
			size_t end = out.find_last_not_of(" \t\r\n");
			out = out.substr(start, end - start + 1);

			// Strip leading bullet markers (e.g. "- ", "* ")
			if (out.size() >= 2 && (out[0] == '-' || out[0] == '*') && out[1] == ' ')
			{
				size_t nextWord = out.find_first_not_of(" \t-*", 0);
				if (nextWord != std::string::npos)
					out = out.substr(nextWord);
			}

			// Strip stray leading list numbers (e.g. "1 ", "2. ", "3) ")
			if (!out.empty() && out[0] >= '0' && out[0] <= '9')
			{
				size_t nonDigit = out.find_first_not_of("0123456789");
				if (nonDigit != std::string::npos && nonDigit <= 3)
				{
					size_t afterMarker = nonDigit;
					if (out[afterMarker] == '.' || out[afterMarker] == ':' || out[afterMarker] == ')' ||
						out[afterMarker] == '-')
					{
						afterMarker++;
					}
					if (afterMarker < out.size() && out[afterMarker] == ' ')
					{
						size_t nextWord = out.find_first_not_of(" \t", afterMarker);
						if (nextWord != std::string::npos)
							out = out.substr(nextWord);
					}
				}
			}

			return out;
		}

		// Ensures text finishes on a complete sentence, trimming any trailing cut-off fragments
		static std::string trimToCompleteSentence(const std::string& text)
		{
			if (text.empty())
				return "";

			auto balanceQuotes = [](std::string s)
			{
				size_t quotes = 0;
				for (char c : s)
				{
					if (c == '"')
						quotes++;
				}
				if (quotes % 2 != 0)
					s += '"';
				return s;
			};

			// Find the last sentence terminator ('.', '!', '?')
			size_t lastPunct = text.find_last_of(".!?");
			if (lastPunct != std::string::npos)
			{
				// If there are closing quotes, brackets, or spaces immediately following, include them
				size_t end = lastPunct + 1;
				while (end < text.size() && (text[end] == '"' || text[end] == ' ' || text[end] == ')'))
				{
					end++;
				}
				std::string sub = text.substr(0, end);
				size_t lastNonSpace = sub.find_last_not_of(" \t\r\n");
				if (lastNonSpace != std::string::npos)
					sub = sub.substr(0, lastNonSpace + 1);
				return balanceQuotes(sub);
			}

			// If no sentence terminator exists, find last word boundary and append a period
			size_t lastSpace = text.rfind(' ');
			if (lastSpace != std::string::npos && lastSpace > 10)
			{
				return balanceQuotes(text.substr(0, lastSpace) + ".");
			}

			return balanceQuotes(text + ".");
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
			WeirdEngine::Logger::log("[LlamaNarrator] Initializing GGUF model: " + modelPath + " (" +
									 std::to_string(fileSize / (1024 * 1024)) + " MB)");

			unload();
			m_cancel.store(false);

			// Silence verbose internal tensor/backend logs from llama.cpp
			llama_log_set(quietLlamaLog, nullptr);

			llama_backend_init();

			llama_model_params modelParams = llama_model_default_params();
			bool useGpu = (m_device == "gpu" || m_device == "cuda" || m_device == "vulkan");
			modelParams.n_gpu_layers = useGpu ? m_gpuLayers : 0;
			m_model = llama_model_load_from_file(modelPath.c_str(), modelParams);
			if (!m_model)
			{
				WeirdEngine::Logger::error("[LlamaNarrator] Error: llama_model_load_from_file failed for: " +
										   modelPath);
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
			m_ctxCapacity = (trainCtx > 0) ? std::min<uint32_t>(512, static_cast<uint32_t>(trainCtx)) : 512;
			ctxParams.n_ctx = m_ctxCapacity;
			ctxParams.n_threads = m_threadCount > 0 ? m_threadCount : 4;
			int hwThreads = static_cast<int>(std::thread::hardware_concurrency());
			ctxParams.n_threads_batch = hwThreads > 0 ? std::max(hwThreads, ctxParams.n_threads) : 8;
			ctxParams.flash_attn = true;
			ctxParams.no_perf = true;

			m_ctx = llama_init_from_model(m_model, ctxParams);
			if (!m_ctx)
			{
				WeirdEngine::Logger::error("[LlamaNarrator] Error: llama_init_from_model failed.");
				llama_model_free(m_model);
				m_model = nullptr;
				return false;
			}

			initSampler();

			m_loadedPath = modelPath;
			WeirdEngine::Logger::log(
				"[LlamaNarrator] GGUF model ready (n_ctx=" + std::to_string(m_ctxCapacity) +
				", threads=" + std::to_string(ctxParams.n_threads) +
				", device=" + (useGpu ? ("gpu (" + std::to_string(modelParams.n_gpu_layers) + " layers)") : "cpu") +
				", flash_attn=" + (ctxParams.flash_attn ? "true" : "false") +
				", seed=" + (m_configuredSeed >= 0 ? std::to_string(m_configuredSeed) : "random") + ")");
			return true;
		}

		void initSampler()
		{
			if (m_sampler)
			{
				llama_sampler_free(m_sampler);
				m_sampler = nullptr;
			}
			// Initialize sampling chain (temp 0.85, min-p 0.05, repetition penalty 1.25)
			// Higher temp + stronger rep penalty = more creative, less repetitive prose from small models
			// min_p outperforms top_p on very small models by cutting nonsense tails more aggressively
			llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
			m_sampler = llama_sampler_chain_init(sparams);
			llama_sampler_chain_add(m_sampler, llama_sampler_init_penalties(256, 1.25f, 0.0f, 0.0f));
			llama_sampler_chain_add(m_sampler, llama_sampler_init_min_p(0.05f, 1));
			llama_sampler_chain_add(m_sampler, llama_sampler_init_temp(0.85f));
			uint32_t seed =
				(m_configuredSeed >= 0)
					? static_cast<uint32_t>(m_configuredSeed)
					: static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
			llama_sampler_chain_add(m_sampler, llama_sampler_init_dist(seed));
		}

		bool isLoaded() const
		{
			return m_model != nullptr && m_ctx != nullptr;
		}

		static std::string formatMoveLog(const MoveAnnotation& ann)
		{
			std::string colorStr = (ann.mover == Color::White) ? "White" : "Black";
			std::string qualityStr = qualityName(ann.quality);
			std::string evalStr = AnnotationWriter::formatScore(ann.evalAfterCp);
			int winPct = static_cast<int>(std::round(ann.winChanceAfter * 100.0f));

			std::string result = colorStr + " played " + (ann.san.empty() ? "?" : ann.san) + " [" + qualityStr +
								 " | eval: " + evalStr + " | win: " + std::to_string(winPct) + "%]";
			if (!ann.title.empty() && ann.title != qualityStr)
				result += " (" + ann.title + ")";
			return result;
		}

		void narrateIntro(StoryStream& out) override
		{
			// Reset history and select/initialize the premise and leaders for this match
			m_storyHistory.clear();
			m_turnIndex = 0;
			m_whiteLeader = "White";
			m_blackLeader = "Black";

			if (!isLoaded())
			{
				m_activePremise = m_configuredPremise.empty() ? std::string(ChessConfig::STORY_INTRO_PLACEHOLDER)
															  : m_configuredPremise;
				out.append(m_activePremise);
				out.setStatus(StoryStatus::Idle);
				WeirdEngine::Logger::log("==================== [Story Opening Premise] ====================");
				WeirdEngine::Logger::log("[Story Premise (Fallback)] " + m_activePremise);
				return;
			}

			out.setStatus(StoryStatus::Generating);

			std::string fullIntro;
			std::string openingPrompt;

			if (!m_configuredPremise.empty())
			{
				// Extract named entities from configured premise (e.g. "dog vs roomba" -> "Dog", "Roomba")
				extractLeaderNames(m_configuredPremise, m_whiteLeader, m_blackLeader);

				const char* chatTemplate = (m_model != nullptr) ? llama_model_chat_template(m_model, nullptr) : nullptr;
				bool isChat = (chatTemplate != nullptr);

				if (isChat)
				{
					std::string systemMsg = "You are a dramatic storyteller. The user will provide a story premise. "
											"Write ONE vivid sentence to continue the story. "
											"Mention both " +
											m_whiteLeader + " and " + m_blackLeader +
											" by name. "
											"No chess terms.";

					std::string userMsg = "Characters: " + m_whiteLeader + " and " + m_blackLeader +
										  ".\n"
										  "Premise: " +
										  m_configuredPremise +
										  "\n"
										  "Write the next sentence to continue the story:";

					openingPrompt = "<|im_start|>system\n" + systemMsg + "<|im_end|>\n<|im_start|>user\n" + userMsg +
									"<|im_end|>\n<|im_start|>assistant\n";

					std::string displayPremise = m_configuredPremise;
					if (!displayPremise.empty())
					{
						if (displayPremise[0] >= 'a' && displayPremise[0] <= 'z')
							displayPremise[0] = static_cast<char>(std::toupper(displayPremise[0]));
						if (displayPremise.back() != '.' && displayPremise.back() != '!' &&
							displayPremise.back() != '?' && displayPremise.back() != '"')
							displayPremise += ".";
					}

					m_activePremise = displayPremise;
					out.startChunk();
					out.appendToCurrentChunk(m_activePremise + " ");

					std::string generatedIntro = generateText(openingPrompt, 50, true, &out);
					std::string cleanIntro = trimToCompleteSentence(sanitizeForEngine(generatedIntro));

					if (!cleanIntro.empty() && cleanIntro.size() >= 8)
					{
						fullIntro = m_activePremise + " " + cleanIntro;
					}
					else
					{
						fullIntro = m_activePremise;
					}
					m_activePremise = fullIntro;
					out.updateCurrentChunk(fullIntro);
				}
				else
				{
					m_activePremise = m_configuredPremise;
					out.startChunk();
					out.appendToCurrentChunk(m_activePremise);
					openingPrompt = m_activePremise + " ";
					std::string generatedIntro = generateText(openingPrompt, 50, true, &out);
					std::string cleanIntro = trimToCompleteSentence(sanitizeForEngine(generatedIntro));

					if (!cleanIntro.empty() && cleanIntro.size() >= 8)
					{
						fullIntro = m_activePremise + " " + cleanIntro;
					}
					else
					{
						fullIntro = m_activePremise;
					}
					m_activePremise = fullIntro;
					out.updateCurrentChunk(fullIntro);
				}
			}
			else
			{
				// Pure AI generation for the opening story premise with structured genre seeds
				const char* chatTemplate = (m_model != nullptr) ? llama_model_chat_template(m_model, nullptr) : nullptr;
				bool isChat = (chatTemplate != nullptr);

				GenrePremise gp = pickRandomGenreSeed();
				m_whiteLeader = gp.white;
				m_blackLeader = gp.black;

				if (isChat)
				{
					std::string systemMsg =
						"You are a storyteller. Write ONE opening sentence establishing a tense standoff. "
						"Mention both " +
						m_whiteLeader + " and " + m_blackLeader + " by name. No chess terms.";

					std::string userMsg = "Characters: " + m_whiteLeader + " and " + m_blackLeader +
										  ".\n"
										  "Setting: " +
										  gp.setting +
										  ".\n"
										  "Write the opening line of their clash:";

					openingPrompt = "<|im_start|>system\n" + systemMsg + "<|im_end|>\n<|im_start|>user\n" + userMsg +
									"<|im_end|>\n<|im_start|>assistant\n";
				}
				else
				{
					openingPrompt = pickRandomCompletionSeed();
				}

				out.startChunk();
				if (!isChat)
				{
					out.appendToCurrentChunk(openingPrompt);
				}

				std::string generatedIntro = generateText(openingPrompt, 50, true, &out);
				std::string cleanIntro = trimToCompleteSentence(sanitizeForEngine(generatedIntro));

				if (!isChat && !cleanIntro.empty())
				{
					fullIntro = openingPrompt + cleanIntro;
				}
				else if (!cleanIntro.empty() && cleanIntro.size() >= 8)
				{
					fullIntro = cleanIntro;
				}
				else
				{
					fullIntro =
						m_whiteLeader + " and " + m_blackLeader + " met " + gp.setting + " for a decisive clash.";
				}
				m_activePremise = fullIntro;
				out.updateCurrentChunk(fullIntro);
			}

			out.setStatus(StoryStatus::Idle);

			WeirdEngine::Logger::log("==================== [Story Opening Premise] ====================");
			if (!openingPrompt.empty())
			{
				WeirdEngine::Logger::log("[LLM Intro Prompt Input] " + openingPrompt);
			}
			WeirdEngine::Logger::log("[Story Intro Output] " + fullIntro);
			// Log structured dataset entry for training
			logDatasetEntry(0, 0, "", "", "", "", "", "", "Opening confrontation", m_activePremise, openingPrompt,
							fullIntro);
		}

		void narrate(const MoveAnnotation& annotation, StoryStream& out) override
		{
			if (!isLoaded())
				return;

			// Option C cadence:
			// Buffer White's standard move and pair it with Black's reply so the story
			// narrates full turns with cohesive action and counter-action.
			bool isCritical =
				annotation.tactics.checkmate || annotation.gameEnded ||
				(annotation.quality == MoveQuality::Blunder && annotation.impact == ImpactLevel::Critical);

			if (annotation.mover == Color::White && !isCritical)
			{
				m_bufferedWhite = annotation;
				return;
			}

			m_turnIndex++;
			out.setStatus(StoryStatus::Generating);

			// 1. Translate chess moves into dramatic conflict action using the active match leaders
			std::string dramaticEvent = formatTurnConflict(m_bufferedWhite, annotation, m_whiteLeader, m_blackLeader);
			std::string whiteSan =
				m_bufferedWhite.has_value() ? (m_bufferedWhite->san.empty() ? "?" : m_bufferedWhite->san) : "";
			std::string whiteQuality = m_bufferedWhite.has_value() ? qualityName(m_bufferedWhite->quality) : "";
			std::string whiteEval =
				m_bufferedWhite.has_value() ? AnnotationWriter::formatScore(m_bufferedWhite->evalAfterCp) : "";

			std::string blackSan = annotation.san.empty() ? "?" : annotation.san;
			std::string blackQuality = qualityName(annotation.quality);
			std::string blackEval = AnnotationWriter::formatScore(annotation.evalAfterCp);

			// 2. Scale max tokens based on game phase (concise, 1-2 punchy sentences)
			int maxTokens = 50;
			if (annotation.fullMoveNumber < 5)
				maxTokens = 45;
			else if (annotation.fullMoveNumber > 25)
				maxTokens = 60;

			// 3. Build rolling context prompt preserving premise + recent narrative history
			std::string leadActor = (m_turnIndex % 2 == 1) ? m_whiteLeader : m_blackLeader;
			std::string contextPrompt = buildContextPrompt(dramaticEvent, maxTokens, leadActor);

			// Log comprehensive turn inputs before inference
			WeirdEngine::Logger::log("-------------------- [Story Turn #" + std::to_string(m_turnIndex) + " (Move " +
									 std::to_string(annotation.fullMoveNumber) + ")] --------------------");
			if (m_bufferedWhite.has_value())
			{
				WeirdEngine::Logger::log("[Chess Input] " + formatMoveLog(*m_bufferedWhite));
			}
			WeirdEngine::Logger::log("[Chess Input] " + formatMoveLog(annotation));
			WeirdEngine::Logger::log("[Action Translation] " + dramaticEvent);
			WeirdEngine::Logger::log("[LLM Prompt Input] " + contextPrompt);

			m_bufferedWhite.reset();

			// 4. Generate story continuation (streaming token pieces directly into active StoryStream chunk)
			out.startChunk();
			std::string generatedText = generateText(contextPrompt, maxTokens, true, &out);
			std::string cleanStory = trimToCompleteSentence(sanitizeForEngine(generatedText));

			if (!cleanStory.empty())
			{
				std::string fullContext = m_activePremise;
				for (const auto& beat : m_storyHistory)
				{
					fullContext += " " + beat;
				}

				m_storyHistory.push_back(cleanStory);
				out.updateCurrentChunk(cleanStory);

				WeirdEngine::Logger::log("[LLM Story Output #" + std::to_string(m_turnIndex) + "] " + cleanStory);
				// Log clean structured dataset entry for model training
				logDatasetEntry(m_turnIndex, annotation.fullMoveNumber, whiteSan, whiteQuality, whiteEval, blackSan,
								blackQuality, blackEval, dramaticEvent, fullContext, contextPrompt, cleanStory);
			}
			else
			{
				// Fallback to dramatic event line if model produces empty output
				std::string fallback = trimToCompleteSentence(sanitizeForEngine(dramaticEvent));
				if (!fallback.empty())
				{
					m_storyHistory.push_back(fallback);
					out.updateCurrentChunk(fallback);
					WeirdEngine::Logger::log("[LLM Story Output #" + std::to_string(m_turnIndex) + " (Fallback)] " +
											 fallback);
				}
			}

			// 5. Update story status based on game climax
			if (annotation.tactics.checkmate || (annotation.gameEnded && annotation.gameState == GameState::Checkmate))
			{
				out.setStatus(StoryStatus::EndedAbruptly);
				logMatchSummary("Checkmate victory");
			}
			else if (annotation.quality == MoveQuality::Blunder && annotation.impact == ImpactLevel::Critical)
			{
				out.setStatus(StoryStatus::EndedAbruptly);
				logMatchSummary("Catastrophic collapse");
			}
			else if (annotation.gameEnded)
			{
				out.setStatus(StoryStatus::EndedNaturally);
				logMatchSummary("Game concluded");
			}
			else
			{
				out.setStatus(StoryStatus::Generating);
			}
		}

	private:
		static void quietLlamaLog(ggml_log_level level, const char* text, void* user_data)
		{
			(void)user_data;
			if (level == GGML_LOG_LEVEL_ERROR && text)
			{
				WeirdEngine::Logger::error(std::string("[llama.cpp] ") + text);
			}
		}

		struct GenrePremise
		{
			std::string white;
			std::string black;
			std::string setting;
		};

		// Picks a specific genre+setting seed with explicit named rivals
		static GenrePremise pickRandomGenreSeed()
		{
			static const std::vector<GenrePremise> seeds = {
				{"Admiral Vance", "Warlord Kael", "in the burning orbit of a dying star"},
				{"Detective Cross", "Boss Moretti", "in a rain-soaked 1940s city alley"},
				{"Captain Flint", "Commodore Sterling", "on stormy Caribbean waters racing for sunken treasure"},
				{"Hacker Nyx", "Enforcer Vector", "battling inside a megacorp neural network"},
				{"Lord Nobunaga", "Ronin Jin", "on a misty wooden bridge at dawn"},
				{"Raider Max", "Warlord Stryker", "fighting over the last clean water oasis"},
				{"Sky Captain Drake", "Air Marshal Vane", "dueling in armored airships above a burning city"},
				{"Archmage Thorne", "Necromancer Malakor", "at the gates of a cursed forgotten academy"},
				{"Sheriff Dalton", "The Rattlesnake Kid", "in a high-noon standoff in a ghost town"},
				{"Sub Commander Hayes", "Captain Volkov", "stalking each other in the frozen ocean depths"},
				{"Chef Luigi", "Chef Pierre", "in a fierce culinary rivalry for the master cup"},
				{"Gladiator Spartacus", "Champion Crassus", "in the blood-soaked Colosseum arena"},
				{"Hunter Rowan", "Bounty Hunter Jax", "crossing paths in a brutal arctic blizzard"},
				{"General Marcus", "Rebel Leader Kira", "on opposite sides of an imperial coup"}};

			auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
			size_t idx = static_cast<size_t>(now) % seeds.size();
			return seeds[idx];
		}

		// For non-chat (completion) models: provides a vivid story-start fragment to complete
		static std::string pickRandomCompletionSeed()
		{
			static const std::vector<std::string> seeds = {
				"Commander Voss aimed the cannon at the approaching fleet, knowing Admiral Crane would ",
				"The necromancer raised the dead army as ",
				"Captain Blackthorn drew his blade on the burning deck, locking eyes with ",
				"In the neon-lit alley, the hacker known as Ghost confronted ",
				"The two warlords met at the edge of the wasteland, each refusing to ",
				"Through the blizzard, the hunter tracked footprints that could only belong to ",
				"Deep beneath the ocean, the submarine lurched as ",
				"The rebel leader climbed the fortress wall at midnight, knowing the general ",
			};

			auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
			size_t idx = static_cast<size_t>(now) % seeds.size();
			return seeds[idx];
		}

		static void extractLeaderNames(const std::string& premise, std::string& whiteLeader, std::string& blackLeader)
		{
			const std::vector<std::string> delimiters = {" vs. ", " vs ", " versus ", " against ", " and "};

			size_t sepPos = std::string::npos;
			size_t sepLen = 0;

			for (const auto& delim : delimiters)
			{
				size_t pos = premise.find(delim);
				if (pos != std::string::npos && (sepPos == std::string::npos || pos < sepPos))
				{
					sepPos = pos;
					sepLen = delim.length();
				}
			}

			if (sepPos != std::string::npos)
			{
				std::string part1 = premise.substr(0, sepPos);
				std::string part2 = premise.substr(sepPos + sepLen);

				auto cleanEntityName = [](const std::string& raw) -> std::string
				{
					std::string s = raw;

					const std::vector<std::string> openingPhrases = {
						"The unexpected confrontation between ",
						"The confrontation between ",
						"The unexpected clash between ",
						"The clash between ",
						"The unexpected showdown between ",
						"The showdown between ",
						"The showdown began when ",
						"The sudden rivalry between ",
						"A sudden rivalry between ",
						"A sudden rivalry broke out when ",
						"Two opposing sides prepared for their decisive clash as ",
						"Two rivals, ",
						"Two contenders, ",
						"Between "};
					for (const auto& op : openingPhrases)
					{
						if (s.find(op) == 0)
						{
							s = s.substr(op.size());
							break;
						}
					}

					const std::vector<std::string> prepPhrases = {" of the ", " of ", " from the ", " from ",
																  " in the ", " in ", " at "};
					for (const auto& prep : prepPhrases)
					{
						size_t p = s.find(prep);
						if (p != std::string::npos && p > 3)
						{
							s = s.substr(0, p);
							break;
						}
					}

					// Take only up to the first newline or punctuation
					size_t breakPos = s.find_first_of("\n\r.?!:;,");
					if (breakPos != std::string::npos)
					{
						s = s.substr(0, breakPos);
					}

					// Remove trailing digits and whitespace
					while (!s.empty() && (s.back() == ' ' || (s.back() >= '0' && s.back() <= '9')))
					{
						s.pop_back();
					}

					const std::vector<std::string> titles = {"Commander ",
															 "Admiral ",
															 "Fleet Commander ",
															 "Warlord ",
															 "Agent ",
															 "Ghost Operative ",
															 "Rogue Operative ",
															 "Commodore ",
															 "Captain ",
															 "High Archmage ",
															 "Void Conjurer ",
															 "Wasteland Raider ",
															 "Iron Citadel Marshal ",
															 "Marshal ",
															 "Grand Inquisitor ",
															 "Inquisitor ",
															 "Rebel Leader ",
															 "Sky Captain ",
															 "Corsair Captain ",
															 "Lord ",
															 "Duke ",
															 "General ",
															 "Chef ",
															 "Dr. ",
															 "Doctor ",
															 "Detective ",
															 "Artist ",
															 "Inventor "};
					for (const auto& t : titles)
					{
						if (s.find(t) == 0)
						{
							s = s.substr(t.size());
							break;
						}
					}

					size_t commaPos = s.find(", ");
					if (commaPos != std::string::npos && commaPos < s.size() - 2)
					{
						s = s.substr(commaPos + 2);
					}

					size_t namedPos = s.find("named ");
					if (namedPos != std::string::npos)
					{
						s = s.substr(namedPos + 6);
					}

					const std::vector<std::string> verbs = {
						" looked",	  " said",	   " asked",   " stepped", " clashed",	" steered", " fought",
						" channeled", " faced",	   " engaged", " met",	   " prepared", " battled", " vied",
						" stared",	  " competed", " crossed", " entered", " began"};
					for (const auto& v : verbs)
					{
						size_t p = s.find(v);
						if (p != std::string::npos)
						{
							s = s.substr(0, p);
							break;
						}
					}

					size_t start = s.find_first_not_of(" \"\'");
					size_t end = s.find_last_not_of(" \"\'");
					if (start != std::string::npos && end != std::string::npos)
						s = s.substr(start, end - start + 1);

					if (!s.empty() && s[0] >= 'a' && s[0] <= 'z')
					{
						s[0] = static_cast<char>(s[0] - ('a' - 'A'));
					}

					return s;
				};

				auto isValidEntityName = [](const std::string& name) -> bool
				{
					if (name.empty() || name.size() > 24)
						return false;
					if (name.find('"') != std::string::npos || name.find('?') != std::string::npos ||
						name.find('!') != std::string::npos || name.find('.') != std::string::npos ||
						name.find(';') != std::string::npos || name.find(':') != std::string::npos)
					{
						return false;
					}
					int words = 0;
					std::istringstream iss(name);
					std::string w;
					while (iss >> w)
						words++;
					return words >= 1 && words <= 3;
				};

				std::string name1 = cleanEntityName(part1);
				std::string name2 = cleanEntityName(part2);

				whiteLeader = isValidEntityName(name1) ? name1 : "White";
				blackLeader = isValidEntityName(name2) ? name2 : "Black";
			}
			else
			{
				whiteLeader = "White";
				blackLeader = "Black";
			}
		}

		static std::string describePieceAction(const MoveAnnotation& ann, const std::string& moverName,
											   const std::string& enemyName)
		{
			if (ann.move.isCastling)
			{
				return moverName + " suddenly shifted to a safer, fortified position.";
			}

			if (ann.tactics.checkmate)
			{
				return moverName + " delivered the killing blow - " + enemyName + " had nowhere left to run.";
			}
			if (ann.tactics.check)
			{
				return moverName + " struck directly at " + enemyName + ", forcing a desperate scramble.";
			}
			if (ann.hasCapture || ann.move.isCapture)
			{
				auto pieceValue = [](PieceType p)
				{
					switch (p)
					{
						case PieceType::Queen:
							return 9;
						case PieceType::Rook:
							return 5;
						case PieceType::Bishop:
							return 3;
						case PieceType::Knight:
							return 3;
						default:
							return 1;
					}
				};

				int capVal = pieceValue(ann.pieceCaptured);
				int movVal = pieceValue(ann.pieceMoved);

				if (ann.pieceCaptured == PieceType::Queen && capVal > movVal)
					return moverName + " delivered a devastating blow, severely crippling " + enemyName + ".";

				if (capVal > movVal)
					return moverName + " executed a brilliant tactical strike, taking down a superior force.";
				else if (capVal == movVal && capVal >= 3)
					return moverName + " clashed head-on with " + enemyName + " in an even exchange of power.";
				else if (capVal >= 3)
					return moverName + " struck a solid hit against " + enemyName + ".";

				return moverName + " chipped away at the defenses of " + enemyName + ".";
			}
			if (ann.tactics.fork || ann.tactics.skewer)
			{
				return moverName + " created a deadly dilemma, threatening multiple targets at once.";
			}
			if (ann.tactics.pin)
			{
				return moverName + " pinned " + enemyName + " in place, cutting off their escape.";
			}

			// Quality-based descriptions with random variants to prevent repetitive rolling history
			int v = static_cast<int>(ann.fullMoveNumber + static_cast<int>(ann.pieceMoved)) % 3;
			switch (ann.quality)
			{
				case MoveQuality::Best:
				{
					const char* opts[] = {" executed a flawless maneuver.",
										  " shifted into a highly advantageous stance.",
										  " took firm control of the engagement."};
					return moverName + std::string(opts[v]);
				}
				case MoveQuality::Excellent:
				{
					const char* opts[] = {" pressed forward with tactical foresight.",
										  " adjusted their approach with calculated efficiency.",
										  " maneuvered into a commanding position."};
					return moverName + std::string(opts[v]);
				}
				case MoveQuality::Good:
				{
					const char* opts[] = {" probed the defenses of %s.",
										  " advanced steadily, testing the resolve of %s.",
										  " shifted their focus, preparing for the next clash with %s."};
					char buf[160];
					snprintf(buf, sizeof(buf), opts[v], enemyName.c_str());
					return moverName + std::string(buf);
				}
				case MoveQuality::Inaccuracy:
				{
					const char* opts[] = {" wavered slightly, their movement landing just off the mark.",
										  " hesitated, drifting out of optimal position.",
										  " misjudged the angle, leaving a slight opening."};
					return moverName + std::string(opts[v]);
				}
				case MoveQuality::Mistake:
				{
					const char* opts[] = {" overextended, leaving a dangerous gap in their defense.",
										  " stumbled, exposing a sudden weakness.",
										  " made a costly misstep, and the cracks began to show."};
					return moverName + std::string(opts[v]);
				}
				case MoveQuality::Blunder:
					return moverName + " made a catastrophic miscalculation, and the tide turned violently.";
				case MoveQuality::Miss:
					return moverName + " hesitated at a critical moment, letting a deadly opportunity slip away.";
				default:
					return moverName + " shifted their position on the battlefield.";
			}
		}

		static std::string formatTurnConflict(const std::optional<MoveAnnotation>& whiteAnn,
											  const MoveAnnotation& blackAnn, const std::string& whiteLeader,
											  const std::string& blackLeader)
		{
			std::string out;
			if (whiteAnn.has_value())
			{
				std::string wDesc = describePieceAction(*whiteAnn, whiteLeader, blackLeader);
				std::string bDesc = describePieceAction(blackAnn, blackLeader, whiteLeader);
				out = wDesc + " " + bDesc;
			}
			else
			{
				const std::string mover = blackAnn.mover == Color::White ? whiteLeader : blackLeader;
				const std::string enemy = blackAnn.mover == Color::White ? blackLeader : whiteLeader;
				out = describePieceAction(blackAnn, mover, enemy);
			}
			return out;
		}

		int countTokens(const std::string& text) const
		{
			if (!m_vocab || text.empty())
				return 0;
			std::vector<llama_token> tokens(text.size() + 16);
			int n = llama_tokenize(m_vocab, text.c_str(), static_cast<int32_t>(text.size()), tokens.data(),
								   static_cast<int32_t>(tokens.size()), true, false);
			if (n < 0)
			{
				tokens.resize(static_cast<size_t>(-n));
				n = llama_tokenize(m_vocab, text.c_str(), static_cast<int32_t>(text.size()), tokens.data(),
								   static_cast<int32_t>(tokens.size()), true, false);
			}
			return std::max(0, n);
		}

		std::string buildContextPrompt(const std::string& dramaticEvent, int maxTokens, const std::string& leadActor)
		{
			// Target token budget for the context prompt
			// Reserve ~150 tokens for the system prompt, ChatML tags, and the dramaticEvent itself.
			int targetHistoryTokens = std::max(48, static_cast<int>(m_ctxCapacity) - maxTokens - 150);

			int premiseTokens = countTokens(m_activePremise);
			targetHistoryTokens = std::max(24, targetHistoryTokens - premiseTokens);

			std::string historyStr;
			int currentHistoryTokens = 0;
			for (auto it = m_storyHistory.rbegin(); it != m_storyHistory.rend(); ++it)
			{
				int lineTokens = countTokens(*it);
				if (currentHistoryTokens + lineTokens > targetHistoryTokens)
					break;
				if (!historyStr.empty())
					historyStr = (*it) + " " + historyStr;
				else
					historyStr = *it;
				currentHistoryTokens += lineTokens;
			}

			const struct llama_model* model = llama_get_model(m_ctx);
			const char* chatTemplate = (model != nullptr) ? llama_model_chat_template(model, nullptr) : nullptr;
			if (chatTemplate != nullptr)
			{
				std::string systemMsg = "You are a narrator recounting an ongoing rivalry between " + m_whiteLeader +
										" and " + m_blackLeader +
										". "
										"Write ONE short action sentence showing what happens next. "
										"Match the intensity of the action: keep it grounded for simple maneuvers, and "
										"save the drama for direct attacks. "
										"Never mention chess.";

				std::string userMsg = "Rivals: " + m_whiteLeader + " vs " + m_blackLeader +
									  "\n"
									  "Story so far:\n" +
									  m_activePremise;
				if (!historyStr.empty())
					userMsg += "\n" + historyStr;
				userMsg += "\n\nWhat just happened:\n" + dramaticEvent + "\n\nWrite ONE new sentence describing what " +
						   leadActor + " does next:";

				return "<|im_start|>system\n" + systemMsg + "<|im_end|>\n<|im_start|>user\n" + userMsg +
					   "<|im_end|>\n<|im_start|>assistant\n";
			}
			else
			{
				std::string prompt = "Continue the story. ";
				if (!m_activePremise.empty())
					prompt += m_activePremise + " ";
				if (!historyStr.empty())
					prompt += historyStr + " ";
				prompt += dramaticEvent + " " + leadActor + " ";
				return prompt;
			}
		}

		std::string generateText(const std::string& prompt, int maxTokens, bool stopAtSentence,
								 StoryStream* stream = nullptr)
		{
			if (!m_model || !m_ctx || !m_vocab || !m_sampler)
				return "";

			m_cancel.store(false);
			llama_sampler_reset(m_sampler);

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

			// Penalize recent story history explicitly to prevent repetitive output.
			// We avoid penalizing the raw prompt tokens from the end because that would penalize the 'dramaticEvent',
			// forcing the model to hallucinate contradictory words to escape the penalty.
			int beatsToPenalize = std::min<int>(static_cast<int>(m_storyHistory.size()), 5);
			for (size_t i = m_storyHistory.size() - beatsToPenalize; i < m_storyHistory.size(); ++i)
			{
				const std::string& beat = m_storyHistory[i];
				std::vector<llama_token> hTokens(beat.size() + 16);
				int hn = llama_tokenize(m_vocab, beat.c_str(), static_cast<int32_t>(beat.size()), hTokens.data(),
										static_cast<int32_t>(hTokens.size()), false, false);
				if (hn > 0)
				{
					for (int k = 0; k < hn; ++k)
						llama_sampler_accept(m_sampler, hTokens[k]);
				}
			}

			// KV Cache Prefix Reuse: check how many prompt tokens match the cached KV prefix
			size_t commonPrefix = 0;
			while (commonPrefix < tokens.size() && commonPrefix < m_cachedPromptTokens.size() &&
				   tokens[commonPrefix] == m_cachedPromptTokens[commonPrefix])
			{
				commonPrefix++;
			}

			// If context would overflow or no prefix match, start clean
			if (commonPrefix == 0 || (tokens.size() + static_cast<size_t>(maxTokens) + 16 >= m_ctxCapacity))
			{
				llama_kv_cache_clear(m_ctx);
				m_cachedPromptTokens.clear();
				commonPrefix = 0;
			}
			else
			{
				// Discard tokens in KV cache beyond the common prefix
				llama_kv_cache_seq_rm(m_ctx, 0, static_cast<llama_pos>(commonPrefix), -1);
			}

			int32_t n_eval = static_cast<int32_t>(tokens.size() - commonPrefix);
			if (n_eval > 0)
			{
				llama_batch batch = llama_batch_get_one(tokens.data() + commonPrefix, n_eval);
				if (llama_decode(m_ctx, batch) != 0)
				{
					// Fallback: clear KV cache and decode full prompt from scratch
					llama_kv_cache_clear(m_ctx);
					m_cachedPromptTokens.clear();
					batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));
					if (llama_decode(m_ctx, batch) != 0)
					{
						return "";
					}
				}
			}

			// Store the evaluated prompt tokens for next turn's prefix match
			m_cachedPromptTokens = tokens;

			std::string result;
			char pieceBuf[128];
			std::string streamPieceBuffer;
			bool isFirstPiece = true;

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
					result += piece;

					if (stream != nullptr)
					{
						std::string cleanPiece = sanitizeForEngine(piece, false);
						if (!cleanPiece.empty())
						{
							streamPieceBuffer += cleanPiece;
							// Emit the first piece immediately to start typewriter without delay, then batch
							if (isFirstPiece || streamPieceBuffer.size() >= 12 ||
								piece.find('.') != std::string::npos || piece.find('!') != std::string::npos ||
								piece.find('?') != std::string::npos || piece.find('\n') != std::string::npos)
							{
								stream->appendToCurrentChunk(streamPieceBuffer);
								streamPieceBuffer.clear();
								isFirstPiece = false;
							}
						}
					}

					// Early stop on natural sentence completion
					if (stopAtSentence && result.size() >= 12)
					{
						if (piece.find('.') != std::string::npos || piece.find('!') != std::string::npos ||
							piece.find('?') != std::string::npos || piece.find('\n') != std::string::npos)
						{
							break;
						}
					}
				}

				llama_token nextTokens[1] = {token};
				llama_batch nextBatch = llama_batch_get_one(nextTokens, 1);
				if (llama_decode(m_ctx, nextBatch) != 0)
					break;
			}

			// Flush any remaining buffered stream pieces
			if (stream != nullptr && !streamPieceBuffer.empty())
			{
				stream->appendToCurrentChunk(streamPieceBuffer);
				streamPieceBuffer.clear();
			}

			return result;
		}

		static std::string escapeJson(const std::string& input)
		{
			std::string output;
			for (char c : input)
			{
				if (c == '"')
					output += "\\\"";
				else if (c == '\\')
					output += "\\\\";
				else if (c == '\n')
					output += " ";
				else if (c == '\r')
					continue;
				else if (c == '\t')
					output += " ";
				else
					output += c;
			}
			return output;
		}

		void logDatasetEntry(int step, int fullMoveNumber, const std::string& whiteSan, const std::string& whiteQuality,
							 const std::string& whiteEval, const std::string& blackSan, const std::string& blackQuality,
							 const std::string& blackEval, const std::string& eventText, const std::string& contextText,
							 const std::string& promptText, const std::string& storyText)
		{
			std::string json =
				"{\"step\":" + std::to_string(step) + ",\"full_move\":" + std::to_string(fullMoveNumber) +
				",\"white_san\":\"" + escapeJson(whiteSan) + "\"" + ",\"white_quality\":\"" + escapeJson(whiteQuality) +
				"\"" + ",\"white_eval\":\"" + escapeJson(whiteEval) + "\"" + ",\"black_san\":\"" +
				escapeJson(blackSan) + "\"" + ",\"black_quality\":\"" + escapeJson(blackQuality) + "\"" +
				",\"black_eval\":\"" + escapeJson(blackEval) + "\"" + ",\"event\":\"" + escapeJson(eventText) + "\"" +
				",\"context\":\"" + escapeJson(contextText) + "\"" + ",\"prompt\":\"" + escapeJson(promptText) + "\"" +
				",\"story\":\"" + escapeJson(storyText) + "\"}";
			WeirdEngine::Logger::log("[Story Dataset Export] " + json);
		}

		void logMatchSummary(const std::string& conclusion)
		{
			WeirdEngine::Logger::log("==================== MATCH STORY CHRONICLE ====================");
			WeirdEngine::Logger::log("Premise: " + m_activePremise);
			for (size_t i = 0; i < m_storyHistory.size(); ++i)
			{
				WeirdEngine::Logger::log("[" + std::to_string(i + 1) + "] " + m_storyHistory[i]);
			}
			WeirdEngine::Logger::log("Conclusion: " + conclusion);
			WeirdEngine::Logger::log("===============================================================");
		}

		void unload()
		{
			m_cancel.store(true);
			m_cachedPromptTokens.clear();
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
		uint32_t m_ctxCapacity = 512;

		std::string m_device = "cpu";
		int m_gpuLayers = 99;
		int64_t m_configuredSeed = -1;
		int m_threadCount = 4;
		std::string m_configuredPremise;
		std::string m_activePremise;
		std::string m_whiteLeader = "White";
		std::string m_blackLeader = "Black";
		std::vector<std::string> m_storyHistory;
		std::vector<llama_token> m_cachedPromptTokens;
		int m_turnIndex = 0;

		std::optional<MoveAnnotation> m_bufferedWhite;
		std::atomic<bool> m_cancel{false};
	};
} // namespace wchess
