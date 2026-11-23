#include "application/config_manager.h"
#include "application/log_manager.h"
#include "model/defect_class.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <map>
#include <vector>
#include <cctype>
#include <memory>

namespace qf {
namespace application {

namespace {

// ---------------------------------------------------------------------------
// 轻量 JSON 解析器（仅覆盖配置所需：object / array / number / string / bool）
// ---------------------------------------------------------------------------
struct JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray  = std::vector<JsonValue>;

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;

    bool        boolValue   = false;
    double      numValue    = 0.0;
    std::string strValue;
    JsonArray   arrayValue;
    JsonObject  objectValue;

    bool isObject() const { return type == Type::Object; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isBool()   const { return type == Type::Bool; }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text), pos_(0) {}

    JsonValue parse() {
        skipWs();
        JsonValue v = parseValue();
        skipWs();
        if (pos_ != text_.size()) {
            throw std::runtime_error("JSON: trailing characters");
        }
        return v;
    }

private:
    void skipWs() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    char peek() const {
        if (pos_ >= text_.size()) throw std::runtime_error("JSON: unexpected end");
        return text_[pos_];
    }

    char get() {
        char c = peek();
        ++pos_;
        return c;
    }

    void expect(char c) {
        if (get() != c) throw std::runtime_error("JSON: unexpected character");
    }

    JsonValue parseValue() {
        char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') {
            JsonValue v;
            v.type = JsonValue::Type::String;
            v.strValue = parseString();
            return v;
        }
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') {
            expect('n'); expect('u'); expect('l'); expect('l');
            JsonValue v;
            v.type = JsonValue::Type::Null;
            return v;
        }
        return parseNumber();
    }

    JsonValue parseObject() {
        expect('{');
        JsonValue v;
        v.type = JsonValue::Type::Object;
        skipWs();
        if (peek() == '}') { get(); return v; }
        while (true) {
            skipWs();
            std::string key = parseString();
            skipWs();
            expect(':');
            skipWs();
            v.objectValue[key] = parseValue();
            skipWs();
            char c = get();
            if (c == '}') break;
            if (c != ',') throw std::runtime_error("JSON: expected ',' or '}'");
        }
        return v;
    }

    JsonValue parseArray() {
        expect('[');
        JsonValue v;
        v.type = JsonValue::Type::Array;
        skipWs();
        if (peek() == ']') { get(); return v; }
        while (true) {
            skipWs();
            v.arrayValue.push_back(parseValue());
            skipWs();
            char c = get();
            if (c == ']') break;
            if (c != ',') throw std::runtime_error("JSON: expected ',' or ']'");
        }
        return v;
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            if (pos_ >= text_.size()) throw std::runtime_error("JSON: unterminated string");
            char c = get();
            if (c == '"') break;
            if (c == '\\') {
                char e = get();
                switch (e) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                default:   out += e;    break;
                }
            } else {
                out += c;
            }
        }
        return out;
    }

    JsonValue parseBool() {
        JsonValue v;
        v.type = JsonValue::Type::Bool;
        if (peek() == 't') {
            expect('t'); expect('r'); expect('u'); expect('e');
            v.boolValue = true;
        } else {
            expect('f'); expect('a'); expect('l'); expect('s'); expect('e');
            v.boolValue = false;
        }
        return v;
    }

    JsonValue parseNumber() {
        size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (pos_ < text_.size() &&
               (std::isdigit(static_cast<unsigned char>(text_[pos_])) ||
                text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E' ||
                text_[pos_] == '+' || text_[pos_] == '-')) {
            ++pos_;
        }
        std::string numStr = text_.substr(start, pos_ - start);
        JsonValue v;
        v.type = JsonValue::Type::Number;
        v.numValue = std::stod(numStr);
        return v;
    }

    const std::string& text_;
    size_t pos_;
};

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

const JsonValue* findMember(const JsonObject& obj, const std::string& key) {
    auto it = obj.find(key);
    return (it != obj.end()) ? &it->second : nullptr;
}

double num(const JsonValue* v, double def) {
    return (v && v->isNumber()) ? v->numValue : def;
}

int numInt(const JsonValue* v, int def) {
    return (v && v->isNumber()) ? static_cast<int>(v->numValue) : def;
}

bool boolean(const JsonValue* v, bool def) {
    return (v && v->isBool()) ? v->boolValue : def;
}

std::string str(const JsonValue* v, const std::string& def) {
    return (v && v->isString()) ? v->strValue : def;
}

void parseAlgorithm(const JsonObject& obj, model::AlgorithmParams& p) {
    p.threshold    = num(findMember(obj, "threshold"), p.threshold);
    p.cannyLow     = numInt(findMember(obj, "canny_low"), p.cannyLow);
    p.cannyHigh    = numInt(findMember(obj, "canny_high"), p.cannyHigh);
    p.blockSize    = numInt(findMember(obj, "block_size"), p.blockSize);
    p.cValue       = numInt(findMember(obj, "c_value"), p.cValue);
    p.histDiff     = num(findMember(obj, "hist_diff"), p.histDiff);
    p.tophatKernel = numInt(findMember(obj, "tophat_kernel"), p.tophatKernel);
    p.blobCountMin = numInt(findMember(obj, "blob_count_min"), p.blobCountMin);
    p.closeKernel  = numInt(findMember(obj, "close_kernel"), p.closeKernel);
    p.aspectRatio  = num(findMember(obj, "aspect_ratio"), p.aspectRatio);
    p.minLength    = numInt(findMember(obj, "min_length"), p.minLength);
    p.metric       = str(findMember(obj, "metric"), p.metric);
}

// ---------------------------------------------------------------------------
// JSON 序列化辅助（与上面的轻量解析器对称）
// ---------------------------------------------------------------------------
std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;      break;
        }
    }
    return out;
}

std::string jstr(const std::string& s) {
    return "\"" + jsonEscape(s) + "\"";
}

std::string jnum(double v) {
    std::ostringstream ss;
    ss.precision(15);
    ss << v;
    return ss.str();
}

std::string jbool(bool v) {
    return v ? "true" : "false";
}

void writeAlgorithm(std::ostringstream& ss, const char* key,
                    const model::AlgorithmParams& p) {
    ss << "      \"" << key << "\": {\n"
       << "        \"metric\": " << jstr(p.metric) << ",\n"
       << "        \"threshold\": " << jnum(p.threshold) << ",\n"
       << "        \"canny_low\": " << p.cannyLow << ",\n"
       << "        \"canny_high\": " << p.cannyHigh << ",\n"
       << "        \"block_size\": " << p.blockSize << ",\n"
       << "        \"c_value\": " << p.cValue << ",\n"
       << "        \"hist_diff\": " << jnum(p.histDiff) << ",\n"
       << "        \"tophat_kernel\": " << p.tophatKernel << ",\n"
       << "        \"blob_count_min\": " << p.blobCountMin << ",\n"
       << "        \"close_kernel\": " << p.closeKernel << ",\n"
       << "        \"aspect_ratio\": " << jnum(p.aspectRatio) << ",\n"
       << "        \"min_length\": " << p.minLength << "\n"
       << "      }";
}

}  // anonymous namespace

model::PipelineParams ConfigManager::defaults() const {
    model::PipelineParams p;
    p.version = 3;

    // 预处理默认全部关闭
    p.preprocess.denoise  = model::DenoiseParams{};
    p.preprocess.contrast = model::ContrastParams{};
    p.preprocess.sharpen  = model::SharpenParams{};

    // 六类算法默认阈值与算子参数（与蓝图 2.3.1 一致）
    model::AlgorithmParams crazing;
    crazing.metric    = "edge_density";
    crazing.threshold = 0.05;
    crazing.cannyLow  = 50;
    crazing.cannyHigh = 150;
    p.algorithms[model::DefectClass::Crazing] = crazing;

    model::AlgorithmParams inclusion;
    inclusion.metric    = "area_ratio";
    inclusion.threshold = 0.08;
    inclusion.blockSize = 31;
    inclusion.cValue    = 3;
    p.algorithms[model::DefectClass::Inclusion] = inclusion;

    model::AlgorithmParams patches;
    patches.metric    = "local_variance";
    patches.threshold = 500.0;
    patches.histDiff  = 0.3;
    p.algorithms[model::DefectClass::Patches] = patches;

    model::AlgorithmParams pitted;
    pitted.metric       = "blob_count";
    pitted.threshold    = 5;
    pitted.tophatKernel = 15;
    pitted.blobCountMin = 5;
    p.algorithms[model::DefectClass::PittedSurface] = pitted;

    model::AlgorithmParams scratches;
    scratches.metric      = "line_length";
    scratches.threshold   = 80;
    scratches.cannyLow    = 50;
    scratches.cannyHigh   = 150;
    scratches.aspectRatio = 3.0;
    scratches.minLength   = 80;
    p.algorithms[model::DefectClass::Scratches] = scratches;

    model::AlgorithmParams ris;
    ris.metric      = "texture_density";
    ris.threshold   = 0.3;
    ris.blockSize   = 21;
    ris.closeKernel = 5;
    p.algorithms[model::DefectClass::RolledInScale] = ris;

    p.decision.mode          = "max_confidence";
    p.decision.minConfidence = 0.3;

    p.gate.timeoutPerImageMs = 5000;
    p.gate.parallelThreads   = 6;

    return p;
}

std::string ConfigManager::toJsonString(const model::PipelineParams& params) const {
    const auto fallbackAlgorithms = defaults().algorithms;

    std::ostringstream ss;
    ss << "{\n"
       << "  \"version\": " << params.version << ",\n"
       << "  \"input_dir\": " << jstr(params.inputDir) << ",\n";

    ss << "  \"preprocess\": {\n";
    ss << "    \"denoise\": {\n"
       << "      \"enable\": " << jbool(params.preprocess.denoise.enable) << ",\n"
       << "      \"method\": " << jstr(params.preprocess.denoise.method) << ",\n"
       << "      \"kernel\": " << params.preprocess.denoise.kernel << "\n"
       << "    },\n";
    ss << "    \"contrast\": {\n"
       << "      \"enable\": " << jbool(params.preprocess.contrast.enable) << ",\n"
       << "      \"method\": " << jstr(params.preprocess.contrast.method) << ",\n"
       << "      \"clip\": " << jnum(params.preprocess.contrast.clip) << "\n"
       << "    },\n";
    ss << "    \"sharpen\": {\n"
       << "      \"enable\": " << jbool(params.preprocess.sharpen.enable) << ",\n"
       << "      \"method\": " << jstr(params.preprocess.sharpen.method) << ",\n"
       << "      \"sigma\": " << jnum(params.preprocess.sharpen.sigma) << "\n"
       << "    }\n"
       << "  },\n";

    ss << "  \"algorithms\": {\n";
    struct ClassKey { model::DefectClass cls; const char* key; };
    const ClassKey classKeys[] = {
        {model::DefectClass::Crazing,       "crazing"},
        {model::DefectClass::Inclusion,     "inclusion"},
        {model::DefectClass::Patches,       "patches"},
        {model::DefectClass::PittedSurface, "pitted_surface"},
        {model::DefectClass::Scratches,     "scratches"},
        {model::DefectClass::RolledInScale, "rolled_in_scale"},
    };
    bool firstAlg = true;
    for (const auto& ck : classKeys) {
        auto it = params.algorithms.find(ck.cls);
        const model::AlgorithmParams& ap =
            (it != params.algorithms.end()) ? it->second : fallbackAlgorithms.at(ck.cls);
        if (!firstAlg) ss << ",\n";
        firstAlg = false;
        writeAlgorithm(ss, ck.key, ap);
    }
    ss << "\n  },\n";

    ss << "  \"decision\": {\n"
       << "    \"mode\": " << jstr(params.decision.mode) << ",\n"
       << "    \"min_confidence\": " << jnum(params.decision.minConfidence) << "\n"
       << "  },\n";

    ss << "  \"gate\": {\n"
       << "    \"timeout_per_image_ms\": " << params.gate.timeoutPerImageMs << ",\n"
       << "    \"parallel_threads\": " << params.gate.parallelThreads << "\n"
       << "  },\n";

    const auto& infra = params.infrastructure;
    ss << "  \"infrastructure\": {\n"
       << "    \"database\": {\n"
       << "      \"enable\": " << jbool(infra.database.enable) << ",\n"
       << "      \"path\": " << jstr(infra.database.path) << "\n"
       << "    },\n"
       << "    \"validation\": {\n"
       << "      \"enable\": " << jbool(infra.validation.enable) << "\n"
       << "    },\n"
       << "    \"comm\": {\n"
       << "      \"enable\": " << jbool(infra.comm.enable) << ",\n"
       << "      \"transport\": " << jstr(infra.comm.transport) << ",\n"
       << "      \"send_class_breakdown\": " << jbool(infra.comm.sendClassBreakdown) << ",\n"
       << "      \"tcp\": {\n"
       << "        \"port\": " << infra.comm.tcp.port << ",\n"
       << "        \"unit_id\": " << infra.comm.tcp.unitId << "\n"
       << "      },\n"
       << "      \"rtu\": {\n"
       << "        \"port\": " << jstr(infra.comm.rtu.port) << ",\n"
       << "        \"baud\": " << infra.comm.rtu.baud << ",\n"
       << "        \"slave_id\": " << infra.comm.rtu.slaveId << "\n"
       << "      }\n"
       << "    }\n"
       << "  }\n"
       << "}\n";

    return ss.str();
}

bool ConfigManager::saveToFile(const std::string& configPath,
                               const model::PipelineParams& params) {
    try {
        std::ofstream out(configPath, std::ios::trunc);
        if (!out.is_open()) {
            LogManager::instance().error("Config save failed: cannot open " + configPath);
            return false;
        }
        out << toJsonString(params);
        if (!out.good()) {
            LogManager::instance().error("Config save failed: write error " + configPath);
            return false;
        }
        LogManager::instance().info("Config saved to: " + configPath);
        return true;
    } catch (const std::exception& e) {
        LogManager::instance().error(std::string("Config save failed: ") + e.what());
        return false;
    }
}

std::optional<model::PipelineParams> ConfigManager::loadFromFile(const std::string& configPath) {
    try {
        auto text = readFile(configPath);
        JsonParser parser(text);
        JsonValue root = parser.parse();
        if (!root.isObject()) {
            throw std::runtime_error("JSON: root is not an object");
        }

        auto params = defaults();

        params.version = numInt(findMember(root.objectValue, "version"), params.version);

        if (const JsonValue* pre = findMember(root.objectValue, "preprocess"); pre && pre->isObject()) {
            const auto& preObj = pre->objectValue;

            if (const JsonValue* dn = findMember(preObj, "denoise"); dn && dn->isObject()) {
                params.preprocess.denoise.enable = boolean(findMember(dn->objectValue, "enable"), params.preprocess.denoise.enable);
                params.preprocess.denoise.method = str(findMember(dn->objectValue, "method"), params.preprocess.denoise.method);
                params.preprocess.denoise.kernel = numInt(findMember(dn->objectValue, "kernel"), params.preprocess.denoise.kernel);
            }
            if (const JsonValue* ct = findMember(preObj, "contrast"); ct && ct->isObject()) {
                params.preprocess.contrast.enable = boolean(findMember(ct->objectValue, "enable"), params.preprocess.contrast.enable);
                params.preprocess.contrast.method = str(findMember(ct->objectValue, "method"), params.preprocess.contrast.method);
                params.preprocess.contrast.clip   = num(findMember(ct->objectValue, "clip"), params.preprocess.contrast.clip);
            }
            if (const JsonValue* sh = findMember(preObj, "sharpen"); sh && sh->isObject()) {
                params.preprocess.sharpen.enable = boolean(findMember(sh->objectValue, "enable"), params.preprocess.sharpen.enable);
                params.preprocess.sharpen.method = str(findMember(sh->objectValue, "method"), params.preprocess.sharpen.method);
                params.preprocess.sharpen.sigma  = num(findMember(sh->objectValue, "sigma"), params.preprocess.sharpen.sigma);
            }
        }

        if (const JsonValue* alg = findMember(root.objectValue, "algorithms"); alg && alg->isObject()) {
            const auto& algObj = alg->objectValue;

            if (const JsonValue* v = findMember(algObj, "crazing"); v && v->isObject())
                parseAlgorithm(v->objectValue, params.algorithms[model::DefectClass::Crazing]);
            if (const JsonValue* v = findMember(algObj, "inclusion"); v && v->isObject())
                parseAlgorithm(v->objectValue, params.algorithms[model::DefectClass::Inclusion]);
            if (const JsonValue* v = findMember(algObj, "patches"); v && v->isObject())
                parseAlgorithm(v->objectValue, params.algorithms[model::DefectClass::Patches]);
            if (const JsonValue* v = findMember(algObj, "pitted_surface"); v && v->isObject())
                parseAlgorithm(v->objectValue, params.algorithms[model::DefectClass::PittedSurface]);
            if (const JsonValue* v = findMember(algObj, "scratches"); v && v->isObject())
                parseAlgorithm(v->objectValue, params.algorithms[model::DefectClass::Scratches]);
            if (const JsonValue* v = findMember(algObj, "rolled_in_scale"); v && v->isObject())
                parseAlgorithm(v->objectValue, params.algorithms[model::DefectClass::RolledInScale]);
        }

        if (const JsonValue* dec = findMember(root.objectValue, "decision"); dec && dec->isObject()) {
            params.decision.mode          = str(findMember(dec->objectValue, "mode"), params.decision.mode);
            params.decision.minConfidence = num(findMember(dec->objectValue, "min_confidence"), params.decision.minConfidence);
        }

        if (const JsonValue* gate = findMember(root.objectValue, "gate"); gate && gate->isObject()) {
            params.gate.timeoutPerImageMs = numInt(findMember(gate->objectValue, "timeout_per_image_ms"), params.gate.timeoutPerImageMs);
            params.gate.parallelThreads   = numInt(findMember(gate->objectValue, "parallel_threads"), params.gate.parallelThreads);
        }

        params.inputDir = str(findMember(root.objectValue, "input_dir"), params.inputDir);

        if (const JsonValue* out = findMember(root.objectValue, "infrastructure"); out && out->isObject()) {
            const auto& outObj = out->objectValue;

            if (const JsonValue* db = findMember(outObj, "database"); db && db->isObject()) {
                params.infrastructure.database.enable = boolean(findMember(db->objectValue, "enable"), params.infrastructure.database.enable);
                params.infrastructure.database.path   = str(findMember(db->objectValue, "path"), params.infrastructure.database.path);
            }
            if (const JsonValue* val = findMember(outObj, "validation"); val && val->isObject()) {
                params.infrastructure.validation.enable = boolean(findMember(val->objectValue, "enable"), params.infrastructure.validation.enable);
            }
            if (const JsonValue* comm = findMember(outObj, "comm"); comm && comm->isObject()) {
                const auto& commObj = comm->objectValue;
                params.infrastructure.comm.enable             = boolean(findMember(commObj, "enable"), params.infrastructure.comm.enable);
                params.infrastructure.comm.transport          = str(findMember(commObj, "transport"), params.infrastructure.comm.transport);
                params.infrastructure.comm.sendClassBreakdown = boolean(findMember(commObj, "send_class_breakdown"), params.infrastructure.comm.sendClassBreakdown);

                if (const JsonValue* tcp = findMember(commObj, "tcp"); tcp && tcp->isObject()) {
                    params.infrastructure.comm.tcp.port   = numInt(findMember(tcp->objectValue, "port"), params.infrastructure.comm.tcp.port);
                    params.infrastructure.comm.tcp.unitId = numInt(findMember(tcp->objectValue, "unit_id"), params.infrastructure.comm.tcp.unitId);
                }
                if (const JsonValue* rtu = findMember(commObj, "rtu"); rtu && rtu->isObject()) {
                    params.infrastructure.comm.rtu.port    = str(findMember(rtu->objectValue, "port"), params.infrastructure.comm.rtu.port);
                    params.infrastructure.comm.rtu.baud    = numInt(findMember(rtu->objectValue, "baud"), params.infrastructure.comm.rtu.baud);
                    params.infrastructure.comm.rtu.slaveId = numInt(findMember(rtu->objectValue, "slave_id"), params.infrastructure.comm.rtu.slaveId);
                }
            }
        }

        LogManager::instance().info("Config loaded from: " + configPath);
        return params;
    } catch (const std::exception& e) {
        LogManager::instance().error(std::string("Config load failed: ") + e.what());
        return std::nullopt;
    }
}

}  // namespace application
}  // namespace qf