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
			m_setting = premise;
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
			if (!m_configuredPremise.empty())
			{
				m_setting = m_configuredPremise;
			}
			if (m_model != nullptr && m_ctx != nullptr)
			{
				initSampler(); // draws a fresh new random seed if m_configuredSeed < 0, or enforces configured seed
			}
			if (m_ctx)
			{
				llama_memory_clear(llama_get_memory(m_ctx), true);
			}
		}

		// Cleans raw generated narrative: strips markdown bolding/codeblocks/prefixes,
		// strips special tokens and chat markers, normalizes smart quotes/dashes,
		// canonicalizes character names, and trims to a complete sentence while preserving apostrophes.
		static std::string cleanNarrativeText(const std::string& raw, const std::string& whiteLeader = "",
											  const std::string& blackLeader = "")
		{
			std::string text = raw;

			auto replaceAll = [](std::string& str, const std::string& from, const std::string& to)
			{
				size_t startPos = 0;
				while ((startPos = str.find(from, startPos)) != std::string::npos)
				{
					str.replace(startPos, from.length(), to);
					startPos += to.length();
				}
			};

			// Strip known special token and chat template markers
			const std::vector<std::string> specialTags = {"<end_of_turn>",
														  "</end_of_turn>",
														  "<end_of_of_turn>",
														  "</end_of_of_turn>",
														  "<start_of_turn>",
														  "</start_of_turn>",
														  "<start_of_turn>user",
														  "<start_of_turn>model",
														  "<|im_end|>",
														  "<|im_start|>",
														  "<|eot_id|>",
														  "<|start_header_id|>",
														  "<|end_header_id|>",
														  "<|begin_of_text|>",
														  "<|end_of_text|>",
														  "<eos>",
														  "<bos>",
														  "<s>",
														  "</s>",
														  "<turn_end>",
														  "<turn_start>",
														  "[INST]",
														  "[/INST]",
														  "<<SYS>>",
														  "<</SYS>>",
														  "&lt;end_of_turn&gt;",
														  "&lt;start_of_turn&gt;"};
			for (const auto& tag : specialTags)
			{
				replaceAll(text, tag, "");
			}

			// Normalize multi-byte UTF-8 punctuation
			replaceAll(text, "\xE2\x80\x98", "'");	 // ‘
			replaceAll(text, "\xE2\x80\x99", "'");	 // ’
			replaceAll(text, "\xE2\x80\x9C", "\"");	 // “
			replaceAll(text, "\xE2\x80\x9D", "\"");	 // ”
			replaceAll(text, "\xE2\x80\x93", "-");	 // en-dash
			replaceAll(text, "\xE2\x80\x94", "-");	 // em-dash
			replaceAll(text, "\xE2\x80\xA6", "..."); // …
			replaceAll(text, "**", "");				 // Strip markdown bold
			replaceAll(text, "__", "");				 // Strip markdown bold
			replaceAll(text, "```", "");			 // Strip code blocks
			replaceAll(text, "`", "");

			// Remove leading hallucinated prefix tags
			const std::vector<std::string> prefixes = {"Story:",
													   "Story :",
													   "Continuation:",
													   "Narrator:",
													   "Narrator :",
													   "Paragraph:",
													   "Assistant:",
													   "assistant:",
													   "Response:",
													   "Turn:",
													   "Action:",
													   "Scene:",
													   "Outcome:",
													   "Beat:",
													   "Note:",
													   "Here is the story:",
													   "Here is the next sentence:",
													   "Here is a sentence:",
													   "Here is one sentence:"};
			for (const auto& prefix : prefixes)
			{
				size_t p = text.find(prefix);
				if (p == 0)
				{
					text = text.substr(prefix.size());
				}
			}

			// Strip leading markdown characters (*, _, #, >, -, etc.) and whitespace/punctuation
			size_t start = text.find_first_not_of(" \t\r\n*_#>-:;.,");
			if (start == std::string::npos)
				return "";
			size_t end = text.find_last_not_of(" \t\r\n*_#");
			text = text.substr(start, end - start + 1);

			// Strip stray leading list numbers (e.g. "1. ", "2) ")
			if (!text.empty() && text[0] >= '0' && text[0] <= '9')
			{
				size_t nonDigit = text.find_first_not_of("0123456789");
				if (nonDigit != std::string::npos && nonDigit <= 3)
				{
					size_t afterMarker = nonDigit;
					if (text[afterMarker] == '.' || text[afterMarker] == ':' || text[afterMarker] == ')' ||
						text[afterMarker] == '-')
					{
						afterMarker++;
					}
					if (afterMarker < text.size() && text[afterMarker] == ' ')
					{
						size_t nextWord = text.find_first_not_of(" \t", afterMarker);
						if (nextWord != std::string::npos)
							text = text.substr(nextWord);
					}
				}
			}

			// Ensure string contains alphabetic characters
			bool hasAlpha = false;
			for (char c : text)
			{
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
				{
					hasAlpha = true;
					break;
				}
			}
			if (!hasAlpha)
				return "";

			// Capitalize first letter of sentence if lowercase
			if (text[0] >= 'a' && text[0] <= 'z')
			{
				text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
			}

			// Normalize casing for rival names if provided
			auto fixLeaderName = [&text](const std::string& leader)
			{
				if (leader.empty() || leader == "White" || leader == "Black" || leader.size() > text.size())
					return;
				std::string lowerText = text;
				std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
				std::string lowerLeader = leader;
				std::transform(lowerLeader.begin(), lowerLeader.end(), lowerLeader.begin(), ::tolower);

				size_t pos = 0;
				while ((pos = lowerText.find(lowerLeader, pos)) != std::string::npos)
				{
					bool validBefore = (pos == 0 || !std::isalpha(static_cast<unsigned char>(text[pos - 1])));
					size_t endPos = pos + lowerLeader.size();
					bool validAfter =
						(endPos == text.size() || !std::isalpha(static_cast<unsigned char>(text[endPos])));
					if (validBefore && validAfter)
					{
						text.replace(pos, leader.length(), leader);
					}
					pos += leader.length();
				}
			};
			fixLeaderName(whiteLeader);
			fixLeaderName(blackLeader);

			return trimToCompleteSentence(text);
		}

		// Sanitizes text to only contain characters supported by WeirdEngine's SDF font
		// (A-Z, a-z, 0-9 and !"&_*()-=+?|.,:;). Apostrophes and non-supported symbols are converted or stripped.
		static std::string sanitizeForEngine(const std::string& raw, bool isFullSentence = true)
		{
			std::string text = raw;

			auto replaceAll = [](std::string& str, const std::string& from, const std::string& to)
			{
				size_t startPos = 0;
				while ((startPos = str.find(from, startPos)) != std::string::npos)
				{
					str.replace(startPos, from.length(), to);
					startPos += to.length();
				}
			};

			// Strip known special token and chat template markers
			const std::vector<std::string> specialTags = {"<end_of_turn>",
														  "</end_of_turn>",
														  "<end_of_of_turn>",
														  "</end_of_of_turn>",
														  "<start_of_turn>",
														  "</start_of_turn>",
														  "<start_of_turn>user",
														  "<start_of_turn>model",
														  "<|im_end|>",
														  "<|im_start|>",
														  "<|eot_id|>",
														  "<|start_header_id|>",
														  "<|end_header_id|>",
														  "<|begin_of_text|>",
														  "<|end_of_text|>",
														  "<eos>",
														  "<bos>",
														  "<s>",
														  "</s>",
														  "<turn_end>",
														  "<turn_start>",
														  "[INST]",
														  "[/INST]",
														  "<<SYS>>",
														  "<</SYS>>",
														  "&lt;end_of_turn&gt;",
														  "&lt;start_of_turn&gt;"};
			for (const auto& tag : specialTags)
			{
				replaceAll(text, tag, "");
			}

			replaceAll(text, "\xE2\x80\x98", "");	 // ‘
			replaceAll(text, "\xE2\x80\x99", "");	 // ’
			replaceAll(text, "\xE2\x80\x9C", "\"");	 // “
			replaceAll(text, "\xE2\x80\x9D", "\"");	 // ”
			replaceAll(text, "\xE2\x80\x93", "-");	 // en-dash
			replaceAll(text, "\xE2\x80\x94", "-");	 // em-dash
			replaceAll(text, "\xE2\x80\xA6", "..."); // …
			replaceAll(text, "'", "");				 // strip standard apostrophes per engine charset rule
			replaceAll(text, "**", "");
			replaceAll(text, "__", "");

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

			// Trim leading/trailing whitespace and markdown formatting
			size_t start = out.find_first_not_of(" \t\r\n*_#>-");
			if (start == std::string::npos)
				return "";
			size_t end = out.find_last_not_of(" \t\r\n*_#");
			out = out.substr(start, end - start + 1);

			// Ensure string contains alphabetic characters
			bool hasAlpha = false;
			for (char c : out)
			{
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
				{
					hasAlpha = true;
					break;
				}
			}
			if (!hasAlpha)
				return "";

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

			return trimToCompleteSentence(out);
		}

		// Ensures text finishes on a complete sentence, trimming any trailing cut-off fragments
		static std::string trimToCompleteSentence(const std::string& text)
		{
			if (text.empty())
				return "";

			// Ensure string has alphanumeric content before formatting
			bool hasAlpha = false;
			for (char c : text)
			{
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
				{
					hasAlpha = true;
					break;
				}
			}
			if (!hasAlpha)
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
			m_ctxCapacity = (trainCtx > 0) ? std::min<uint32_t>(2048, static_cast<uint32_t>(trainCtx)) : 2048;
			ctxParams.n_ctx = m_ctxCapacity;
			ctxParams.n_threads = m_threadCount > 0 ? m_threadCount : 4;
			int hwThreads = static_cast<int>(std::thread::hardware_concurrency());
			ctxParams.n_threads_batch = hwThreads > 0 ? std::max(hwThreads, ctxParams.n_threads) : 8;
			ctxParams.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_ENABLED;
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
				", flash_attn=" + std::string(llama_flash_attn_type_name(ctxParams.flash_attn_type)) +
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
			// Initialize sampling chain (temp 0.80, top-p 0.90, min-p 0.05, repetition penalty 1.20)
			// Cuts off repetitive echo loops while providing creative, grounded sentences and preserving named
			// entities.
			llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
			m_sampler = llama_sampler_chain_init(sparams);
			llama_sampler_chain_add(m_sampler, llama_sampler_init_penalties(256, 1.20f, 0.10f, 0.10f));
			llama_sampler_chain_add(m_sampler, llama_sampler_init_min_p(0.05f, 1));
			llama_sampler_chain_add(m_sampler, llama_sampler_init_top_p(0.90f, 1));
			llama_sampler_chain_add(m_sampler, llama_sampler_init_temp(0.80f));
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
			// Reset history and select/initialize the premise, setting and leaders for this match
			m_storyHistory.clear();
			m_turnIndex = 0;
			m_whiteLeader = "White";
			m_blackLeader = "Black";
			m_setting = "in an intense clash";

			if (!isLoaded())
			{
				if (!m_configuredPremise.empty())
				{
					extractLeaderNames(m_configuredPremise, m_whiteLeader, m_blackLeader);
					m_activePremise = m_configuredPremise;
				}
				else
				{
					GenrePremise gp = pickRandomGenreSeed();
					m_setting = gp.setting;
					generateProceduralRivals(m_setting, m_whiteLeader, m_blackLeader);
					m_activePremise =
						m_whiteLeader + " and " + m_blackLeader + " met for a decisive clash (" + m_setting + ").";
				}
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
				// Extract named entities from configured premise (e.g. "Captain Flint vs Commodore Sterling on stormy
				// Caribbean waters")
				extractLeaderNames(m_configuredPremise, m_whiteLeader, m_blackLeader);
				m_setting = m_configuredPremise;

				const char* chatTemplate = (m_model != nullptr) ? llama_model_chat_template(m_model, nullptr) : nullptr;
				bool isChat = (chatTemplate != nullptr);

				if (isChat)
				{
					std::string systemMsg =
						"You are a storyteller. The user will provide a story premise.\n"
						"Write ONE subtle, grounded opening sentence establishing a quiet, tense standoff between " +
						m_whiteLeader + " and " + m_blackLeader +
						" by name.\n"
						"Focus on the sensory details of the surroundings. Avoid cliches like starting with 'The air'. "
						"No dramatic action yet. No chess terms.";

					std::string userMsg = "Characters: " + m_whiteLeader + " and " + m_blackLeader +
										  ".\n"
										  "Premise: " +
										  m_configuredPremise +
										  "\n"
										  "Write the opening line of their clash:";

					openingPrompt = formatChatPrompt(systemMsg, userMsg);

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
					out.appendToCurrentChunk(sanitizeForEngine(m_activePremise) + " ");

					std::string generatedIntro = generateText(openingPrompt, 50, true, &out);
					std::string cleanIntro = cleanNarrativeText(generatedIntro, m_whiteLeader, m_blackLeader);

					if (!cleanIntro.empty() && cleanIntro.size() >= 8)
					{
						fullIntro = m_activePremise + " " + cleanIntro;
					}
					else
					{
						fullIntro = m_activePremise;
					}
					m_activePremise = fullIntro;
					out.updateCurrentChunk(sanitizeForEngine(fullIntro));
				}
				else
				{
					m_activePremise = m_configuredPremise;
					out.startChunk();
					out.appendToCurrentChunk(sanitizeForEngine(m_activePremise));
					openingPrompt = m_activePremise + " ";
					std::string generatedIntro = generateText(openingPrompt, 50, true, &out);
					std::string cleanIntro = cleanNarrativeText(generatedIntro, m_whiteLeader, m_blackLeader);

					if (!cleanIntro.empty() && cleanIntro.size() >= 8)
					{
						fullIntro = m_activePremise + " " + cleanIntro;
					}
					else
					{
						fullIntro = m_activePremise;
					}
					m_activePremise = fullIntro;
					out.updateCurrentChunk(sanitizeForEngine(fullIntro));
				}
			}
			else
			{
				// Pure AI generation for the opening story premise with structured genre seeds
				const char* chatTemplate = (m_model != nullptr) ? llama_model_chat_template(m_model, nullptr) : nullptr;
				bool isChat = (chatTemplate != nullptr);

				GenrePremise gp = pickRandomGenreSeed();
				m_setting = gp.setting;

				if (isChat)
				{
					// Step 1: Prompt the model to invent setting-specific names for the rivals
					std::string nameSys = "You are a creative writer. In this setting (" + gp.setting +
										  "), "
										  "invent TWO distinct, flavorful rival characters, commanders, or codenames "
										  "(1 to 3 words each).\n"
										  "Respond ONLY with their names separated by ' vs '.\n"
										  "Example: Captain Avery vs Commodore Vance";
					std::string nameUser = "Setting: " + gp.setting + "\nRivals:";
					std::string namePrompt = formatChatPrompt(nameSys, nameUser);
					std::string rivalsText = generateText(namePrompt, 25, false, nullptr);

					extractLeaderNames(rivalsText, m_whiteLeader, m_blackLeader);

					if (m_whiteLeader == "White" || m_blackLeader == "Black" || m_whiteLeader == m_blackLeader)
					{
						generateProceduralRivals(gp.setting, m_whiteLeader, m_blackLeader);
					}

					// Step 2: Write the opening atmospheric standoff sentence featuring the rivals
					std::string systemMsg =
						"You are a storyteller. Write ONE calm opening sentence establishing a quiet, tense standoff "
						"between " +
						m_whiteLeader + " and " + m_blackLeader + " in this setting (" + gp.setting +
						").\n"
						"Mention both " +
						m_whiteLeader + " and " + m_blackLeader +
						" by name.\n"
						"Focus on the sensory details of the surroundings. Avoid cliches like starting with 'The air'. "
						"No dramatic action yet. No chess terms.";

					std::string userMsg = "Characters: " + m_whiteLeader + " and " + m_blackLeader +
										  ".\n"
										  "Setting: " +
										  gp.setting +
										  ".\n"
										  "Write the opening line of their clash:";

					openingPrompt = formatChatPrompt(systemMsg, userMsg);
				}
				else
				{
					generateProceduralRivals(gp.setting, m_whiteLeader, m_blackLeader);
					openingPrompt = pickRandomCompletionSeed();
				}

				out.startChunk();
				if (!isChat)
				{
					out.appendToCurrentChunk(sanitizeForEngine(openingPrompt));
				}

				std::string generatedIntro = generateText(openingPrompt, 50, true, &out);
				std::string cleanIntro = cleanNarrativeText(generatedIntro, m_whiteLeader, m_blackLeader);

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
						m_whiteLeader + " and " + m_blackLeader + " met for a decisive clash (" + gp.setting + ").";
				}
				m_activePremise = fullIntro;
				out.updateCurrentChunk(sanitizeForEngine(fullIntro));
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
			bool isCheckmate =
				annotation.tactics.checkmate || (annotation.gameEnded && annotation.gameState == GameState::Checkmate);
			bool isGameOver = isCheckmate || annotation.gameEnded;
			bool isCritical = isGameOver || (annotation.quality == MoveQuality::Blunder &&
											 annotation.impact == ImpactLevel::Critical);

			if (annotation.mover == Color::White && !isCritical)
			{
				m_bufferedWhite = annotation;
				return;
			}

			m_turnIndex++;
			out.setStatus(StoryStatus::Generating);

			// 1. Translate chess moves into dramatic conflict action with explicit actor attribution
			std::string dramaticEvent = formatTurnConflict(m_bufferedWhite, annotation, m_whiteLeader, m_blackLeader);
			std::string whiteSan =
				m_bufferedWhite.has_value() ? (m_bufferedWhite->san.empty() ? "?" : m_bufferedWhite->san) : "";
			std::string whiteQuality = m_bufferedWhite.has_value() ? qualityName(m_bufferedWhite->quality) : "";
			std::string whiteEval =
				m_bufferedWhite.has_value() ? AnnotationWriter::formatScore(m_bufferedWhite->evalAfterCp) : "";

			std::string blackSan = annotation.san.empty() ? "?" : annotation.san;
			std::string blackQuality = qualityName(annotation.quality);
			std::string blackEval = AnnotationWriter::formatScore(annotation.evalAfterCp);

			// 2. Determine prompt type, token budget, and sentence constraints
			int maxTokens = 50;
			std::string contextPrompt;
			bool stopAtSentence = true;

			if (isGameOver)
			{
				maxTokens = 90; // allow 2-3 dramatic concluding sentences for the climax
				stopAtSentence = false;
				contextPrompt = buildEpiloguePrompt(dramaticEvent, maxTokens, annotation);
			}
			else
			{
				if (annotation.fullMoveNumber < 5)
					maxTokens = 45;
				else if (annotation.fullMoveNumber > 25)
					maxTokens = 60;
				stopAtSentence = true;
				contextPrompt = buildContextPrompt(dramaticEvent, maxTokens, annotation);
			}

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

			// 3. Generate story continuation (streaming token pieces directly into active StoryStream chunk)
			out.startChunk();
			std::string generatedText = generateText(contextPrompt, maxTokens, stopAtSentence, &out);
			std::string cleanStory = cleanNarrativeText(generatedText, m_whiteLeader, m_blackLeader);

			if (!cleanStory.empty() && cleanStory.size() >= 8)
			{
				std::string fullContext = m_activePremise;
				for (const auto& beat : m_storyHistory)
				{
					fullContext += " " + beat;
				}

				m_storyHistory.push_back(cleanStory);
				out.updateCurrentChunk(sanitizeForEngine(cleanStory));

				WeirdEngine::Logger::log("[LLM Story Output #" + std::to_string(m_turnIndex) + "] " + cleanStory);
				// Log clean structured dataset entry for model training
				logDatasetEntry(m_turnIndex, annotation.fullMoveNumber, whiteSan, whiteQuality, whiteEval, blackSan,
								blackQuality, blackEval, dramaticEvent, fullContext, contextPrompt, cleanStory);
			}
			else
			{
				// Fallback to clean narrative prose if model produces empty or invalid output
				std::string fallback =
					formatTurnFallbackProse(m_bufferedWhite, annotation, m_whiteLeader, m_blackLeader);
				std::string cleanFallback = cleanNarrativeText(fallback, m_whiteLeader, m_blackLeader);
				if (!cleanFallback.empty())
				{
					std::string fullContext = m_activePremise;
					for (const auto& beat : m_storyHistory)
					{
						fullContext += " " + beat;
					}

					m_storyHistory.push_back(cleanFallback);
					out.updateCurrentChunk(sanitizeForEngine(cleanFallback));
					WeirdEngine::Logger::log("[LLM Story Output #" + std::to_string(m_turnIndex) + " (Fallback)] " +
											 cleanFallback);
					logDatasetEntry(m_turnIndex, annotation.fullMoveNumber, whiteSan, whiteQuality, whiteEval, blackSan,
									blackQuality, blackEval, dramaticEvent, fullContext, contextPrompt, cleanFallback);
				}
			}

			m_bufferedWhite.reset();

			// 4. Update story status based on game climax
			if (isCheckmate)
			{
				out.setStatus(StoryStatus::EndedAbruptly);
				logMatchSummary("Checkmate victory");
			}
			else if (annotation.gameEnded)
			{
				if (annotation.quality == MoveQuality::Blunder && annotation.impact == ImpactLevel::Critical)
				{
					out.setStatus(StoryStatus::EndedAbruptly);
					logMatchSummary("Catastrophic collapse");
				}
				else
				{
					out.setStatus(StoryStatus::EndedNaturally);
					logMatchSummary("Game concluded");
				}
			}
			else
			{
				out.setStatus(StoryStatus::Generating);
			}
		}

		std::string testBuildContextPrompt(const std::string& dramaticEvent, int maxTokens, const MoveAnnotation& ann)
		{
			return buildContextPrompt(dramaticEvent, maxTokens, ann);
		}

		std::string testBuildEpiloguePrompt(const std::string& dramaticEvent, int maxTokens, const MoveAnnotation& ann)
		{
			return buildEpiloguePrompt(dramaticEvent, maxTokens, ann);
		}

		void addStoryBeat(const std::string& beat)
		{
			m_storyHistory.push_back(beat);
		}

		void setActivePremise(const std::string& premise)
		{
			m_activePremise = premise;
		}

		void setLeaders(const std::string& white, const std::string& black)
		{
			m_whiteLeader = white;
			m_blackLeader = black;
		}

		static std::string testDescribePieceAction(const MoveAnnotation& ann, const std::string& moverName,
												   const std::string& enemyName)
		{
			return describePieceAction(ann, moverName, enemyName);
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
			std::string setting;
		};

		// Picks a vague yet imaginative setting seed
		static GenrePremise pickRandomGenreSeed()
		{
			static const std::vector<GenrePremise> seeds = {{"pirates"},
															{"science fiction"},
															{"comedy"},
															{"unexpected adversaries"},
															{"cyberpunk noir"},
															{"high fantasy"},
															{"gothic horror"},
															{"wild west"},
															{"space opera"},
															{"steampunk Victorian"},
															{"mythological duel"},
															{"post-apocalyptic wasteland"},
															{"feudal samurai drama"},
															{"deep sea expedition"},
															{"superheroes and villains"},
															{"espionage thriller"},
															{"culinary showdown"},
															{"ancient gladiators"},
															{"courtroom drama"},
															{"interdimensional rift"},
															{"medieval siege"},
															{"dystopian rebellion"},
															{"arctic survival"},
															{"paranormal mystery"},
															{"martial arts tournament"},
															{"fairy tale with a dark twist"},
															{"time travel paradox"},
															{"corporate espionage"},
															{"bureaucratic absurdity"}};

			auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
			size_t idx = static_cast<size_t>(now) % seeds.size();
			return seeds[idx];
		}

		// For non-chat (completion) models: provides a vivid story-start fragment to complete
		static std::string pickRandomCompletionSeed()
		{
			static const std::vector<std::string> seeds = {
				"The opposing fleet appeared along the horizon as the vanguard prepared to ",
				"The dark forces gathered at the border while the scouts watched from ",
				"On the stormy deck, the two captains locked eyes as ",
				"In the neon-lit alleyway, the rogue operative confronted ",
				"The two rival commanders met at the edge of the contested ground, refusing to ",
				"Through the blinding snow, the advance patrol tracked movement ahead as ",
				"Deep beneath the surface, the vessels closed the distance while ",
				"At the fortress gates at midnight, the defenders watched as "};

			auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
			size_t idx = static_cast<size_t>(now) % seeds.size();
			return seeds[idx];
		}

		static void generateProceduralRivals(const std::string& setting, std::string& whiteLeader,
											 std::string& blackLeader)
		{
			(void)setting;
			static const std::vector<std::string> titlesA = {
				"Agent",  "Captain",  "Commander",	"Director", "Warlord", "Marshal",
				"Archon", "Shadow",	  "Cipher",		"Vanguard", "Baron",   "Inquisitor",
				"Oracle", "Overseer", "Chancellor", "Provost",	"Consul",  "Envoy"};
			static const std::vector<std::string> titlesB = {
				"Operative", "Commodore", "General", "Handler",	 "Chieftain", "Enforcer",
				"Magister",	 "Specter",	  "Vector",	 "Champion", "Count",	  "Justiciar",
				"Phantom",	 "Sentience", "Prefect", "Reeve",	 "Tribune",	  "Legate"};
			static const std::vector<std::string> rootsA = {
				"Vane",	 "Cross", "Kael",	"Sterling", "Dorne",   "Drake",	 "Voss", "Radek", "Thorne", "Valen",
				"Kovak", "Soren", "Mercer", "Rostov",	"Ashford", "Zephyr", "Onyx", "Solas", "Gideon", "Corvus"};
			static const std::vector<std::string> rootsB = {"Moretti", "Kane",	 "Gable",	"Stryker",	 "Malakor",
															"Volkov",  "Crane",	 "Morales", "Vance",	 "Nyx",
															"Brandt",  "Graves", "Helios",	"Vesper",	 "Sinclair",
															"Vortex",  "Cinder", "Krell",	"Balthazar", "Drakos"};

			auto now = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
			size_t iA = (now ^ (now >> 16)) % titlesA.size();
			size_t iB = ((now >> 8) ^ (now >> 24)) % titlesB.size();
			size_t rA = ((now >> 4) ^ (now >> 12)) % rootsA.size();
			size_t rB = ((now >> 14) ^ (now >> 28)) % rootsB.size();

			whiteLeader = titlesA[iA] + " " + rootsA[rA];
			blackLeader = titlesB[iB] + " " + rootsB[rB];
			if (whiteLeader == blackLeader)
			{
				blackLeader = titlesB[(iB + 1) % titlesB.size()] + " " + rootsB[(rB + 1) % rootsB.size()];
			}
		}

		static void extractLeaderNames(const std::string& premise, std::string& whiteLeader, std::string& blackLeader)
		{
			const std::vector<std::string> delimiters = {" vs. ", " vs ",	  " vs. \n",   " vs\n", " VS ",
														 " Vs ",  " versus ", " against ", " and ", "\n- ",
														 "\n2. ", "\n",		  " / ",	   " - "};

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
						"Between ",
						"1. ",
						"2. ",
						"- ",
						"* "};
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

					size_t start = s.find_first_not_of(" \"\'*`");
					size_t end = s.find_last_not_of(" \"\'*`");
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
					if (name.empty() || name.size() > 30)
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
			(void)moverName;
			if (ann.move.isCastling)
			{
				return "shifted into a fortified defensive redoubt, locking down the rear position.";
			}

			bool isMoverLowMaterial = (ann.mover == Color::White) ? (ann.whitePieces > 0 && ann.whitePieces <= 2)
																  : (ann.blackPieces > 0 && ann.blackPieces <= 2);

			if (ann.tactics.checkmate)
			{
				return "struck the decisive finishing blow - " + enemyName + " had no escape.";
			}
			if (ann.tactics.check)
			{
				if (isMoverLowMaterial)
					return "struck back with a desperate counter-assault against " + enemyName + ".";
				return "launched a direct assault against " + enemyName + ", forcing an urgent defensive scramble.";
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

				if (isMoverLowMaterial)
				{
					return "struck in a desperate last stand, cutting down an advancing enemy before falling back.";
				}

				if (ann.pieceCaptured == PieceType::Queen && capVal > movVal)
					return "struck down " + enemyName + "'s flagship champion in a devastating strike.";

				if (capVal > movVal)
					return "executed a calculated strike, eliminating a superior " + enemyName + " detachment.";
				else if (capVal == movVal && capVal >= 3)
					return "clashed directly with " + enemyName + " in an even exchange of heavy forces.";
				else if (capVal >= 3)
					return "breached " + enemyName + "'s defensive perimeter, taking out an important outpost.";

				return "chipped away at " + enemyName + "'s forward screen, claiming a frontline position.";
			}
			if (ann.tactics.fork || ann.tactics.skewer)
			{
				return "executed a dual-pronged strike, threatening multiple " + enemyName + " positions at once.";
			}
			if (ann.tactics.pin)
			{
				return "pinned " + enemyName + "'s forces in place, severely restricting their movement.";
			}

			// Piece-specific actions based on move quality and piece type:
			uint32_t seedIdx = static_cast<uint32_t>(ann.fullMoveNumber * 7 + static_cast<int>(ann.pieceMoved) * 11 +
													 static_cast<int>(ann.quality) * 17);

			if (isMoverLowMaterial || ann.pieceMoved == PieceType::King)
			{
				if (ann.quality == MoveQuality::Best || ann.quality == MoveQuality::Excellent)
				{
					const char* opts[] = {"retreated evasively, maneuvering as a lone survivor dodging pursuit.",
										  "sought a defensible refuge, skillfully evading the closing net.",
										  "maneuvered to an open pocket, staying on the move to survive."};
					return std::string(opts[seedIdx % 3]);
				}
				if (ann.quality == MoveQuality::Good)
				{
					const char* opts[] = {"backed away into a cautious stance, bracing against the closing circle.",
										  "shifted alone across the perimeter, staying out of immediate reach."};
					return std::string(opts[seedIdx % 2]);
				}
				if (ann.quality == MoveQuality::Inaccuracy || ann.quality == MoveQuality::Mistake)
				{
					const char* opts[] = {"backed into a constricted angle with dwindling escape routes.",
										  "hesitated under heavy pressure, losing crucial evasive ground."};
					return std::string(opts[seedIdx % 2]);
				}
				if (ann.quality == MoveQuality::Blunder)
				{
					return "stumbled into a cornered pocket, leaving no remaining path to flee.";
				}
				return "retreated cautiously across the contested area.";
			}

			if (ann.pieceMoved == PieceType::Pawn)
			{
				if (ann.quality == MoveQuality::Best || ann.quality == MoveQuality::Excellent)
				{
					const char* opts[] = {"advanced a vanguard unit to secure central ground.",
										  "claimed forward space with disciplined positioning.",
										  "anchored the frontline formation with steady momentum.",
										  "pushed infantry forward to establish an assertive presence."};
					return std::string(opts[seedIdx % 4]);
				}
				if (ann.quality == MoveQuality::Good)
				{
					const char* opts[] = {"pushed a forward scout to test the perimeter.",
										  "staked an opening claim on the forward line.",
										  "stepped forward steadily to gauge the distance.",
										  "advanced frontline pickets toward the middle zone."};
					return std::string(opts[seedIdx % 4]);
				}
				if (ann.quality == MoveQuality::Inaccuracy)
				{
					return "pushed forward on an unconventional flank angle, leaving an awkward gap.";
				}
				if (ann.quality == MoveQuality::Mistake)
				{
					return "overextended on the flank, exposing a weak seam in the vanguard.";
				}
			}
			else if (ann.pieceMoved == PieceType::Knight)
			{
				if (ann.quality == MoveQuality::Best || ann.quality == MoveQuality::Excellent)
				{
					const char* opts[] = {"maneuvered swift mobile riders into an active forward post.",
										  "dispatched a flexible detachment to command key intersection angles.",
										  "repositioned mobile cavalry to threaten the enemy flank."};
					return std::string(opts[seedIdx % 3]);
				}
				if (ann.quality == MoveQuality::Good)
				{
					const char* opts[] = {"steered mobile units along the contested flank.",
										  "shifted cavalry inward to support the main body."};
					return std::string(opts[seedIdx % 2]);
				}
			}
			else if (ann.pieceMoved == PieceType::Bishop)
			{
				if (ann.quality == MoveQuality::Best || ann.quality == MoveQuality::Excellent)
				{
					const char* opts[] = {"opened long-range sightlines across the open diagonal.",
										  "positioned sharpshooters with a clear view across the field.",
										  "established an aggressive diagonal corridor pointing toward enemy lines."};
					return std::string(opts[seedIdx % 3]);
				}
				if (ann.quality == MoveQuality::Good)
				{
					const char* opts[] = {"developed long-range support along the open diagonal.",
										  "shifted firing lines to cover the central expanse."};
					return std::string(opts[seedIdx % 2]);
				}
			}
			else if (ann.pieceMoved == PieceType::Rook)
			{
				if (ann.quality == MoveQuality::Best || ann.quality == MoveQuality::Excellent)
				{
					const char* opts[] = {"brought heavy artillery into position along an open file.",
										  "seized control of a critical transit channel with heavy support.",
										  "aligned heavy batteries to command the direct pathway."};
					return std::string(opts[seedIdx % 3]);
				}
			}
			else if (ann.pieceMoved == PieceType::Queen)
			{
				if (ann.quality == MoveQuality::Best || ann.quality == MoveQuality::Excellent)
				{
					const char* opts[] = {"deployed the flagship champion into high-threat territory.",
										  "commanded the central field with an imposing show of force.",
										  "realigned elite vanguard forces to pressure multiple fronts."};
					return std::string(opts[seedIdx % 3]);
				}
			}

			// General quality-based descriptions
			switch (ann.quality)
			{
				case MoveQuality::Best:
				{
					const char* opts[] = {"executed a precise, calculated maneuver to gain positional advantage.",
										  "secured a clean, well-fortified foothold in the field.",
										  "strengthened overall command across the theater.",
										  "optimized formation lines with disciplined tactical poise."};
					return std::string(opts[seedIdx % 4]);
				}
				case MoveQuality::Excellent:
				{
					const char* opts[] = {"advanced with sharp efficiency, tightening control.",
										  "adjusted deployment with steady, confident precision.",
										  "claimed valuable territory across the contested boundary."};
					return std::string(opts[seedIdx % 3]);
				}
				case MoveQuality::Good:
				{
					const char* opts[] = {"surveyed enemy lines with a cautious advance.",
										  "advanced steadily, maintaining watchful coverage over opposing units.",
										  "realigned formation to prepare for the next tactical phase."};
					return std::string(opts[seedIdx % 3]);
				}
				case MoveQuality::Inaccuracy:
				{
					const char* opts[] = {"drifted slightly off optimal formation, yielding minor positioning.",
										  "hesitated momentarily, stepping into an awkward alignment.",
										  "misjudged the closing distance, making a slight concession."};
					return std::string(opts[seedIdx % 3]);
				}
				case MoveQuality::Mistake:
				{
					const char* opts[] = {"overextended recklessly, opening a visible vulnerability.",
										  "stumbled in deployment, giving up hard-fought ground.",
										  "conceded critical positioning to opposing forces."};
					return std::string(opts[seedIdx % 3]);
				}
				case MoveQuality::Blunder:
					if (ann.pieceMoved == PieceType::Queen)
						return "sent their flagship champion directly into a fatal, catastrophic trap.";
					return "committed a severe tactical blunder, leaving defenses dangerously compromised.";
				case MoveQuality::Miss:
					return "hesitated at the critical moment, allowing a decisive opening to slip away.";
				default:
					return "shifted position across the contested field.";
			}
		}

		static std::string formatTurnFallbackProse(const std::optional<MoveAnnotation>& whiteAnn,
												   const MoveAnnotation& blackAnn, const std::string& whiteLeader,
												   const std::string& blackLeader)
		{
			if (whiteAnn.has_value())
			{
				std::string wDesc = describePieceAction(*whiteAnn, whiteLeader, blackLeader);
				std::string bDesc = describePieceAction(blackAnn, blackLeader, whiteLeader);
				return whiteLeader + " " + wDesc + " Meanwhile, " + blackLeader + " " + bDesc;
			}
			else
			{
				const std::string mover = blackAnn.mover == Color::White ? whiteLeader : blackLeader;
				const std::string enemy = blackAnn.mover == Color::White ? blackLeader : whiteLeader;
				return mover + " " + describePieceAction(blackAnn, mover, enemy);
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
				out = "- " + whiteLeader + ": " + wDesc + "\n- " + blackLeader + ": " + bDesc;
			}
			else
			{
				const std::string mover = blackAnn.mover == Color::White ? whiteLeader : blackLeader;
				const std::string enemy = blackAnn.mover == Color::White ? blackLeader : whiteLeader;
				out = "- " + mover + ": " + describePieceAction(blackAnn, mover, enemy);
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

		std::string formatChatPrompt(const std::string& systemMsg, const std::string& userMsg) const
		{
			const char* chatTemplate = (m_model != nullptr) ? llama_model_chat_template(m_model, nullptr) : nullptr;
			if (chatTemplate != nullptr)
			{
				// 1. Try applying model's native template with separate system and user messages
				std::vector<llama_chat_message> msgs;
				if (!systemMsg.empty())
				{
					msgs.push_back({"system", systemMsg.c_str()});
				}
				msgs.push_back({"user", userMsg.c_str()});

				int32_t needed = llama_chat_apply_template(chatTemplate, msgs.data(), msgs.size(), true, nullptr, 0);
				if (needed > 0)
				{
					std::string formatted(static_cast<size_t>(needed + 1), '\0');
					int32_t written =
						llama_chat_apply_template(chatTemplate, msgs.data(), msgs.size(), true, formatted.data(),
												  static_cast<int32_t>(formatted.size()));
					if (written > 0)
					{
						formatted.resize(static_cast<size_t>(written));
						return formatted;
					}
				}

				// 2. If system role is not supported by the template (e.g. Gemma 1/2/4), combine system + user into
				// single user message
				std::string combinedUser = systemMsg.empty() ? userMsg : (systemMsg + "\n\n" + userMsg);
				llama_chat_message userOnlyMsg = {"user", combinedUser.c_str()};
				needed = llama_chat_apply_template(chatTemplate, &userOnlyMsg, 1, true, nullptr, 0);
				if (needed > 0)
				{
					std::string formatted(static_cast<size_t>(needed + 1), '\0');
					int32_t written = llama_chat_apply_template(chatTemplate, &userOnlyMsg, 1, true, formatted.data(),
																static_cast<int32_t>(formatted.size()));
					if (written > 0)
					{
						formatted.resize(static_cast<size_t>(written));
						return formatted;
					}
				}
			}

			// 3. Fallback to model-family explicit formatting
			std::string tmpl = (chatTemplate != nullptr) ? std::string(chatTemplate) : "";
			std::string combinedUser = systemMsg.empty() ? userMsg : (systemMsg + "\n\n" + userMsg);

			if (tmpl.find("start_of_turn") != std::string::npos || tmpl.find("gemma") != std::string::npos ||
				m_loadedPath.find("gemma") != std::string::npos || m_loadedPath.find("Gemma") != std::string::npos)
			{
				return "<start_of_turn>user\n" + combinedUser + "<end_of_turn>\n<start_of_turn>model\n";
			}
			else if (tmpl.find("<|start_header_id|>") != std::string::npos ||
					 m_loadedPath.find("llama-3") != std::string::npos ||
					 m_loadedPath.find("Llama-3") != std::string::npos)
			{
				std::string s = "<|begin_of_text|>";
				if (!systemMsg.empty())
					s += "<|start_header_id|>system<|end_header_id|>\n\n" + systemMsg + "<|eot_id|>";
				s += "<|start_header_id|>user<|end_header_id|>\n\n" + userMsg +
					 "<|eot_id|><|start_header_id|>assistant<|end_header_id|>\n\n";
				return s;
			}
			else
			{
				return "<|im_start|>system\n" + systemMsg + "<|im_end|>\n<|im_start|>user\n" + userMsg +
					   "<|im_end|>\n<|im_start|>assistant\n";
			}
		}

		std::string buildContextPrompt(const std::string& dramaticEvent, int maxTokens, const MoveAnnotation& ann)
		{
			// Target token budget for the context prompt with 2048 token capacity
			int targetHistoryTokens = std::max(128, static_cast<int>(m_ctxCapacity) - maxTokens - 250);

			int premiseTokens = countTokens(m_activePremise);
			targetHistoryTokens = std::max(64, targetHistoryTokens - premiseTokens);

			std::string historyStr;
			int currentHistoryTokens = 0;
			int turnsIncluded = 0;
			for (auto it = m_storyHistory.rbegin(); it != m_storyHistory.rend(); ++it)
			{
				int lineTokens = countTokens(*it);
				if (currentHistoryTokens + lineTokens > targetHistoryTokens || turnsIncluded >= 4)
					break;
				if (!historyStr.empty())
					historyStr = (*it) + "\n" + historyStr;
				else
					historyStr = *it;
				currentHistoryTokens += lineTokens;
				turnsIncluded++;
			}

			std::string materialContext;
			if (ann.whitePieces <= 2 && ann.blackPieces <= 2) {
				materialContext = "Both sides have been reduced to lone survivors. Focus on the exhausted, intimate, desperate duel. Do NOT use army-scale descriptors like 'disciplined lines', 'formations', 'reserves', 'contingents', or 'outer barriers'.\n";
			} else if (ann.whitePieces <= 2) {
				materialContext = m_whiteLeader + " is severely depleted, fighting alone for survival against overwhelming forces. Do NOT use army-scale descriptors for " + m_whiteLeader + " like 'disciplined lines', 'formations', 'reserves', or 'contingents'. Frame their actions as evasive retreats or desperate last stands.\n";
			} else if (ann.blackPieces <= 2) {
				materialContext = m_blackLeader + " is severely depleted, fighting alone for survival against overwhelming forces. Do NOT use army-scale descriptors for " + m_blackLeader + " like 'disciplined lines', 'formations', 'reserves', or 'contingents'. Frame their actions as evasive retreats or desperate last stands.\n";
			}

			const struct llama_model* model = llama_get_model(m_ctx);
			const char* chatTemplate = (model != nullptr) ? llama_model_chat_template(model, nullptr) : nullptr;
			if (chatTemplate != nullptr)
			{
				std::string settingContext = m_setting.empty() ? "" : (" (" + m_setting + ")");
				std::string systemMsg =
					"You are a vivid narrator chronicling the clash between " + m_whiteLeader + " and " +
					m_blackLeader + settingContext +
					".\n"
					"Write ONE concise, grounded sentence narrating this turn. Ground all actions in the specific elements of the setting.\n"
					"Match the true scale of the action: opening moves are subtle, cautious, and measured. "
					"Save dramatic turning points, major blunders, and significant setbacks strictly for direct captures, checks, "
					"and critical mistakes.\n"
					"Each actor performed their respective action. Do not mix up who acted. "
					"Use fresh, varied phrasing and avoid repeating stock phrases from previous turns. "
					"Do not invent new major events beyond what just happened. Never mention chess terms.\n" + materialContext;

				std::string userMsg = "Rivals: " + m_whiteLeader + " vs " + m_blackLeader + "\n";
				if (!m_setting.empty())
					userMsg += "Setting: " + m_setting + "\n";
				userMsg += "Story so far:\n" + m_activePremise;
				if (!historyStr.empty())
					userMsg += "\n" + historyStr;
				userMsg += "\n\nWhat just happened:\n" + dramaticEvent +
						   "\n\nWrite ONE grounded sentence capturing this exchange in this setting:";

				return formatChatPrompt(systemMsg, userMsg);
			}
			else
			{
				std::string prompt = "Continue the story in the setting (" + m_setting + "). ";
				if (!m_activePremise.empty())
					prompt += m_activePremise + " ";
				if (!historyStr.empty())
					prompt += historyStr + " ";
				prompt += dramaticEvent + " ";
				return prompt;
			}
		}

		std::string buildEpiloguePrompt(const std::string& dramaticEvent, int maxTokens, const MoveAnnotation& ann)
		{
			int targetHistoryTokens = std::max(128, static_cast<int>(m_ctxCapacity) - maxTokens - 250);

			int premiseTokens = countTokens(m_activePremise);
			targetHistoryTokens = std::max(64, targetHistoryTokens - premiseTokens);

			std::string historyStr;
			int currentHistoryTokens = 0;
			int turnsIncluded = 0;
			for (auto it = m_storyHistory.rbegin(); it != m_storyHistory.rend(); ++it)
			{
				int lineTokens = countTokens(*it);
				if (currentHistoryTokens + lineTokens > targetHistoryTokens || turnsIncluded >= 4)
					break;
				if (!historyStr.empty())
					historyStr = (*it) + "\n" + historyStr;
				else
					historyStr = *it;
				currentHistoryTokens += lineTokens;
				turnsIncluded++;
			}

			std::string winner = (ann.mover == Color::White) ? m_whiteLeader : m_blackLeader;
			std::string loser = (ann.mover == Color::White) ? m_blackLeader : m_whiteLeader;

			std::string outcomeStr;
			if (ann.tactics.checkmate) {
				outcomeStr = winner + " has delivered the final blow and won the match.";
			} else if (ann.tactics.stalemate || ann.gameState == GameState::Stalemate) {
				outcomeStr = winner + " held overwhelming supremacy, but through a fatal misstep boxed the opponent into an untargetable position, ending in an unresolved, bitter stalemate.";
			} else if (ann.gameState == GameState::InsufficientMaterial || ann.gameState == GameState::FiftyMoveRule || ann.gameState == GameState::ThreefoldRepetition || ann.tactics.draw) {
				outcomeStr = "The long conflict has ground to a halt, ending in an exhausted draw with no decisive victor.";
			} else {
				outcomeStr = loser + " has resigned or collapsed, yielding victory to " + winner + ".";
			}

			const struct llama_model* model = llama_get_model(m_ctx);
			const char* chatTemplate = (model != nullptr) ? llama_model_chat_template(model, nullptr) : nullptr;
			if (chatTemplate != nullptr)
			{
				std::string settingContext = m_setting.empty() ? "" : (" (" + m_setting + ")");
				std::string systemMsg =
					"You are a master storyteller bringing the conflict between " + m_whiteLeader + " and " + m_blackLeader +
					settingContext + " to its conclusion.\n" + outcomeStr + "\n"
					"Write 1-2 vivid, dramatic concluding sentences providing thematic closure, depicting the final state of the match "
					"and the resolution of their confrontation in a way that fits this specific setting.\n"
					"Never mention chess terms.";

				std::string userMsg = "Rivals: " + m_whiteLeader + " vs " + m_blackLeader + "\n";
				if (!m_setting.empty())
					userMsg += "Setting: " + m_setting + "\n";
				userMsg += "Story so far:\n" + m_activePremise;
				if (!historyStr.empty())
					userMsg += "\n" + historyStr;
				userMsg += "\n\nDecisive Climax:\n" + dramaticEvent +
						   "\n\nWrite 2-3 dramatic concluding sentences resolving the conflict:";

				return formatChatPrompt(systemMsg, userMsg);
			}
			else
			{
				std::string prompt = outcomeStr + " (" + m_setting + "). ";
				if (!m_activePremise.empty())
					prompt += m_activePremise + " ";
				if (!historyStr.empty())
					prompt += historyStr + " ";
				prompt += dramaticEvent + " ";
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
				llama_memory_clear(llama_get_memory(m_ctx), true);
				m_cachedPromptTokens.clear();
				commonPrefix = 0;
			}
			else
			{
				// Discard tokens in memory beyond the common prefix
				llama_memory_seq_rm(llama_get_memory(m_ctx), 0, static_cast<llama_pos>(commonPrefix), -1);
			}

			int32_t n_eval = static_cast<int32_t>(tokens.size() - commonPrefix);
			if (n_eval > 0)
			{
				llama_batch batch = llama_batch_get_one(tokens.data() + commonPrefix, n_eval);
				if (llama_decode(m_ctx, batch) != 0)
				{
					// Fallback: clear memory and decode full prompt from scratch
					llama_memory_clear(llama_get_memory(m_ctx), true);
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

			const std::vector<std::string> stopTags = {"<end_of_turn>",
													   "</end_of_turn>",
													   "<start_of_turn>",
													   "</start_of_turn>",
													   "<start_of_turn>user",
													   "<start_of_turn>model",
													   "<|im_end|>",
													   "<|im_start|>",
													   "<|eot_id|>",
													   "<|start_header_id|>",
													   "<|end_header_id|>",
													   "<|begin_of_text|>",
													   "<|end_of_text|>",
													   "<eos>",
													   "<bos>",
													   "<s>",
													   "</s>",
													   "<turn_end>",
													   "<turn_start>",
													   "[INST]",
													   "[/INST]"};

			for (int i = 0; i < maxTokens; ++i)
			{
				if (m_cancel.load())
					break;

				llama_token token = llama_sampler_sample(m_sampler, m_ctx, -1);
				llama_sampler_accept(m_sampler, token);

				if (token == LLAMA_TOKEN_NULL)
					break;

				if (llama_vocab_is_eog(m_vocab, token) || llama_vocab_is_control(m_vocab, token) ||
					token == llama_vocab_eot(m_vocab) || token == llama_vocab_eos(m_vocab))
				{
					break;
				}

				int n_piece = llama_token_to_piece(m_vocab, token, pieceBuf, sizeof(pieceBuf), 0, false);
				if (n_piece > 0)
				{
					std::string piece(pieceBuf, static_cast<size_t>(n_piece));

					// Check if piece contains special/control token markers
					bool hasStopTag = false;
					for (const auto& tag : stopTags)
					{
						if (piece.find(tag) != std::string::npos)
						{
							hasStopTag = true;
							break;
						}
					}
					if (hasStopTag)
						break;

					result += piece;

					// Check if accumulated result contains any stop tags
					size_t tagPos = std::string::npos;
					for (const auto& tag : stopTags)
					{
						size_t pos = result.find(tag);
						if (pos != std::string::npos)
						{
							if (tagPos == std::string::npos || pos < tagPos)
								tagPos = pos;
						}
					}
					if (tagPos != std::string::npos)
					{
						result = result.substr(0, tagPos);
						break;
					}

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
		uint32_t m_ctxCapacity = 2048;

		std::string m_device = "cpu";
		int m_gpuLayers = 99;
		int64_t m_configuredSeed = -1;
		int m_threadCount = 4;
		std::string m_configuredPremise;
		std::string m_activePremise;
		std::string m_setting = "in an intense clash";
		std::string m_whiteLeader = "White";
		std::string m_blackLeader = "Black";
		std::vector<std::string> m_storyHistory;
		std::vector<llama_token> m_cachedPromptTokens;
		int m_turnIndex = 0;

		std::optional<MoveAnnotation> m_bufferedWhite;
		std::atomic<bool> m_cancel{false};
	};
} // namespace wchess
