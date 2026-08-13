#include "editor/NodeGraphFramework.hpp"
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
    static float pointToSegmentDistance(ImVec2 p, ImVec2 a, ImVec2 b) {
        ImVec2 ab = ImVec2(b.x - a.x, b.y - a.y);
        ImVec2 ap = ImVec2(p.x - a.x, p.y - a.y);
        float lenSq = ab.x * ab.x + ab.y * ab.y;
        if (lenSq < 1e-5f) return std::sqrt(ap.x * ap.x + ap.y * ap.y);
        float t = std::max(0.0f, std::min(1.0f, (ap.x * ab.x + ap.y * ab.y) / lenSq));
        ImVec2 proj = ImVec2(a.x + t * ab.x, a.y + t * ab.y);
        ImVec2 diff = ImVec2(p.x - proj.x, p.y - proj.y);
        return std::sqrt(diff.x * diff.x + diff.y * diff.y);
    }

    static float pointToBezierDistance(ImVec2 p, ImVec2 p1, ImVec2 c1, ImVec2 c2, ImVec2 p2, int numSamples = 12) {
        float minDist = 1e9f;
        ImVec2 prev = p1;
        for (int i = 1; i <= numSamples; ++i) {
            float t = (float)i / (float)numSamples;
            float invT = 1.0f - t;
            ImVec2 curr = ImVec2(
                invT*invT*invT * p1.x + 3.0f*invT*invT*t * c1.x + 3.0f*invT*t*t * c2.x + t*t*t * p2.x,
                invT*invT*invT * p1.y + 3.0f*invT*invT*t * c1.y + 3.0f*invT*t*t * c2.y + t*t*t * p2.y
            );
            minDist = std::min(minDist, pointToSegmentDistance(p, prev, curr));
            prev = curr;
        }
        return minDist;
    }


    struct JsonVal {
        enum Type { Null, Bool, Number, String, Array, Object } type = Null;
        bool boolVal = false;
        double numVal = 0.0;
        std::string strVal;
        std::vector<JsonVal> arrVal;
        std::vector<std::pair<std::string, JsonVal>> objVal;

        const JsonVal& operator[](const std::string& key) const {
            if (type == Object) {
                for (const auto& pair : objVal) {
                    if (pair.first == key) return pair.second;
                }
            }
            static JsonVal dummy;
            return dummy;
        }

        bool has(const std::string& key) const {
            if (type == Object) {
                for (const auto& pair : objVal) {
                    if (pair.first == key) return true;
                }
            }
            return false;
        }
    };

    // Simple JSON parsing scanner
    JsonVal parseJson(const std::string& src, size_t& pos) {
        auto skipWhitespace = [&]() {
            while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\r' || src[pos] == '\n')) {
                pos++;
            }
        };

        skipWhitespace();
        if (pos >= src.size()) return {};

        if (src[pos] == '{') {
            pos++;
            JsonVal val;
            val.type = JsonVal::Object;
            while (true) {
                skipWhitespace();
                if (pos < src.size() && src[pos] == '}') { pos++; break; }
                if (pos >= src.size()) break;
                JsonVal keyVal = parseJson(src, pos);
                if (keyVal.type != JsonVal::String) break;
                skipWhitespace();
                if (pos < src.size() && src[pos] == ':') pos++;
                JsonVal valueVal = parseJson(src, pos);
                val.objVal.push_back({ keyVal.strVal, valueVal });
                skipWhitespace();
                if (pos < src.size() && src[pos] == ',') { pos++; }
                else if (pos < src.size() && src[pos] == '}') { pos++; break; }
                else { break; }
            }
            return val;
        } else if (src[pos] == '[') {
            pos++;
            JsonVal val;
            val.type = JsonVal::Array;
            while (true) {
                skipWhitespace();
                if (pos < src.size() && src[pos] == ']') { pos++; break; }
                if (pos >= src.size()) break;
                val.arrVal.push_back(parseJson(src, pos));
                skipWhitespace();
                if (pos < src.size() && src[pos] == ',') { pos++; }
                else if (pos < src.size() && src[pos] == ']') { pos++; break; }
                else { break; }
            }
            return val;
        } else if (src[pos] == '"') {
            pos++;
            size_t start = pos;
            while (pos < src.size() && src[pos] != '"') {
                if (src[pos] == '\\' && pos + 1 < src.size()) pos++;
                pos++;
            }
            std::string s = src.substr(start, pos - start);
            if (pos < src.size()) pos++;
            JsonVal val;
            val.type = JsonVal::String;
            val.strVal = s;
            return val;
        } else if (src[pos] == 't' || src[pos] == 'f') {
            bool isTrue = (src[pos] == 't');
            pos += isTrue ? 4 : 5;
            JsonVal val;
            val.type = JsonVal::Bool;
            val.boolVal = isTrue;
            return val;
        } else if (src[pos] == 'n') {
            pos += 4;
            return {};
        } else {
            size_t start = pos;
            if (src[pos] == '-') pos++;
            while (pos < src.size() && ((src[pos] >= '0' && src[pos] <= '9') || src[pos] == '.' || src[pos] == 'e' || src[pos] == 'E' || src[pos] == '+' || src[pos] == '-')) {
                pos++;
            }
            std::string numStr = src.substr(start, pos - start);
            JsonVal val;
            val.type = JsonVal::Number;
            try { val.numVal = std::stod(numStr); } catch (...) { val.numVal = 0.0; }
            return val;
        }
    }
}

namespace Engine {

    // -------------------------------------------------------------------------
    // Pin type constants available to all graph editors
    // -------------------------------------------------------------------------
    static const NodePinType PIN_TYPE_FLOW      = { "flow",      IM_COL32(255, 255, 255, 255) };
    static const NodePinType PIN_TYPE_FLOAT     = { "float",     IM_COL32(156, 220,  80, 255) };
    static const NodePinType PIN_TYPE_INT       = { "int",       IM_COL32( 67, 198, 255, 255) };
    static const NodePinType PIN_TYPE_BOOL      = { "bool",      IM_COL32(220, 100,  80, 255) };
    static const NodePinType PIN_TYPE_STRING    = { "string",    IM_COL32(255, 200,  80, 255) };
    static const NodePinType PIN_TYPE_VEC3      = { "Vec3",      IM_COL32(120, 100, 220, 255) };
    static const NodePinType PIN_TYPE_DIALOGUE  = { "dialogue",  IM_COL32(200,  80, 220, 255) };
    static const NodePinType PIN_TYPE_STATE     = { "state",     IM_COL32(255, 165,  80, 255) };

    // -------------------------------------------------------------------------
    NodeGraph::NodeGraph() {}
    NodeGraph::~NodeGraph() {}

    uint32_t NodeGraph::generateId() { return m_nextId++; }

    // -------------------------------------------------------------------------
    void NodeGraph::setNodeHeaderColor(uint32_t nodeId, ImU32 color) {
        if (Node* n = findNode(nodeId)) n->headerColor = color;
    }

    // -------------------------------------------------------------------------
    uint32_t NodeGraph::createNode(const std::string& title, const std::string& typeName, ImVec2 pos) {
        Node node;
        node.id = generateId();
        node.title = title;
        node.typeName = typeName;
        node.position = pos;
        node.size = ImVec2(180.0f, 60.0f);
        m_nodes.push_back(node);
        return node.id;
    }

    uint32_t NodeGraph::addInputPin(uint32_t nodeId, const std::string& name, const NodePinType& type) {
        Node* node = findNode(nodeId);
        if (!node) return 0;
        NodePin pin;
        pin.id = generateId();
        pin.name = name;
        pin.type = type;
        pin.isOutput = false;
        pin.nodeId = nodeId;
        node->inputs.push_back(pin);
        return pin.id;
    }

    uint32_t NodeGraph::addOutputPin(uint32_t nodeId, const std::string& name, const NodePinType& type) {
        Node* node = findNode(nodeId);
        if (!node) return 0;
        NodePin pin;
        pin.id = generateId();
        pin.name = name;
        pin.type = type;
        pin.isOutput = true;
        pin.nodeId = nodeId;
        node->outputs.push_back(pin);
        return pin.id;
    }

    void NodeGraph::addLink(uint32_t pinAId, uint32_t pinBId) {
        NodePin* pA = findPin(pinAId);
        NodePin* pB = findPin(pinBId);
        if (!pA || !pB) return;
        if (pA->nodeId == pB->nodeId) return;

        NodePin* from = pA->isOutput ? pA : (pB->isOutput ? pB : nullptr);
        NodePin* to   = !pA->isOutput ? pA : (!pB->isOutput ? pB : nullptr);
        if (!from || !to) return;
        if (from->type.name != to->type.name) return;

        uint32_t fromPinId = from->id;
        uint32_t toPinId   = to->id;

        // Prevent duplicate link between same output and input pin pair
        for (const auto& l : m_links) {
            if (l.fromPinId == fromPinId && l.toPinId == toPinId) return;
        }

        NodeLink link;
        link.id = generateId();
        link.fromPinId = fromPinId;
        link.toPinId   = toPinId;
        m_links.push_back(link);

        if (onLinkCreated) {
            onLinkCreated(fromPinId, toPinId);
        }
    }

    void NodeGraph::removeLink(uint32_t linkId) {
        auto it = std::find_if(m_links.begin(), m_links.end(),
            [linkId](const NodeLink& l) { return l.id == linkId; });
        if (it != m_links.end()) {
            if (onLinkDeleted) {
                onLinkDeleted(it->fromPinId, it->toPinId);
            }
            if (m_selectedLinkId == linkId) {
                m_selectedLinkId = 0;
            }
            m_links.erase(it);
        }
    }


    void NodeGraph::deleteNode(uint32_t nodeId) {
        // Remove all connected links (firing callbacks)
        std::vector<NodeLink> toRemove;
        for (const auto& l : m_links) {
            NodePin* from = findPin(l.fromPinId);
            NodePin* to   = findPin(l.toPinId);
            if ((from && from->nodeId == nodeId) || (to && to->nodeId == nodeId)) {
                toRemove.push_back(l);
            }
        }
        for (const auto& l : toRemove) {
            if (onLinkDeleted) onLinkDeleted(l.fromPinId, l.toPinId);
            removeLink(l.id);
        }

        m_nodes.erase(
            std::remove_if(m_nodes.begin(), m_nodes.end(),
                [nodeId](const Node& n) { return n.id == nodeId; }),
            m_nodes.end());

        if (m_selectedNodeId == nodeId) {
            m_selectedNodeId = 0;
            if (onNodeSelected) onNodeSelected(0);
        }

        if (onNodeDeleted) onNodeDeleted(nodeId);
    }

    void NodeGraph::clear() {
        m_nodes.clear();
        m_links.clear();
        m_nextId = 1;
        m_selectedNodeId = 0;
        m_draggingPinId  = 0;
    }

    Node* NodeGraph::findNode(uint32_t nodeId) {
        for (auto& n : m_nodes) if (n.id == nodeId) return &n;
        return nullptr;
    }
    NodePin* NodeGraph::findPin(uint32_t pinId) {
        for (auto& n : m_nodes) {
            for (auto& p : n.inputs)  if (p.id == pinId) return &p;
            for (auto& p : n.outputs) if (p.id == pinId) return &p;
        }
        return nullptr;
    }
    NodeLink* NodeGraph::findLink(uint32_t linkId) {
        for (auto& l : m_links) if (l.id == linkId) return &l;
        return nullptr;
    }
    NodeLink* NodeGraph::findLinkConnectingToInput(uint32_t inputPinId) {
        for (auto& l : m_links) if (l.toPinId == inputPinId) return &l;
        return nullptr;
    }

    // -------------------------------------------------------------------------
    void NodeGraph::registerNodeType(const std::string& category,
                                     const std::string& typeName,
                                     const std::string& title,
                                     std::function<void(uint32_t)> populateCallback,
                                     std::function<void(Node&)> customWidgetCallback,
                                     std::function<void(Node&)> detailPanelCallback) {
        for (auto& entry : m_registeredTypes) {
            if (entry.typeName == typeName) {
                entry.category = category;
                entry.title = title;
                entry.populateCallback = populateCallback;
                entry.customWidgetCallback = customWidgetCallback;
                entry.detailPanelCallback = detailPanelCallback;
                return;
            }
        }
        m_registeredTypes.push_back({ category, typeName, title,
                                      populateCallback, customWidgetCallback, detailPanelCallback });
    }


    // =========================================================================
    // Serialization
    // =========================================================================
    std::string NodeGraph::serialize() {
        std::stringstream ss;
        ss << "{\n";
        ss << "  \"nextId\": " << m_nextId << ",\n";
        ss << "  \"nodes\": [\n";
        for (size_t i = 0; i < m_nodes.size(); ++i) {
            const auto& node = m_nodes[i];
            ss << "    {\n";
            ss << "      \"id\": " << node.id << ",\n";
            ss << "      \"title\": \"" << node.title << "\",\n";
            ss << "      \"typeName\": \"" << node.typeName << "\",\n";
            ss << "      \"x\": " << node.position.x << ",\n";
            ss << "      \"y\": " << node.position.y << ",\n";
            ss << "      \"headerColor\": " << node.headerColor << ",\n";

            std::string escData;
            for (char c : node.customData) {
                if      (c == '"')  escData += "\\\"";
                else if (c == '\\') escData += "\\\\";
                else if (c == '\n') escData += "\\n";
                else                escData += c;
            }
            ss << "      \"customData\": \"" << escData << "\",\n";

            auto writePins = [&](const std::vector<NodePin>& pins) {
                ss << "[\n";
                for (size_t j = 0; j < pins.size(); ++j) {
                    const auto& pin = pins[j];
                    ss << "        { \"id\": " << pin.id
                       << ", \"name\": \"" << pin.name
                       << "\", \"type\": \"" << pin.type.name
                       << "\", \"color\": " << pin.type.color
                       << ", \"fVal\": " << pin.floatVal
                       << ", \"iVal\": " << pin.intVal
                       << ", \"bVal\": " << (pin.boolVal ? 1 : 0)
                       << ", \"sVal\": \"" << pin.stringVal << "\" }";
                    if (j + 1 < pins.size()) ss << ",";
                    ss << "\n";
                }
                ss << "      ]";
            };

            ss << "      \"inputs\": ";  writePins(node.inputs);  ss << ",\n";
            ss << "      \"outputs\": "; writePins(node.outputs); ss << "\n";
            ss << "    }";
            if (i + 1 < m_nodes.size()) ss << ",";
            ss << "\n";
        }
        ss << "  ],\n";
        ss << "  \"links\": [\n";
        for (size_t i = 0; i < m_links.size(); ++i) {
            const auto& link = m_links[i];
            ss << "    { \"id\": " << link.id
               << ", \"from\": " << link.fromPinId
               << ", \"to\": "   << link.toPinId << " }";
            if (i + 1 < m_links.size()) ss << ",";
            ss << "\n";
        }
        ss << "  ]\n";
        ss << "}\n";
        return ss.str();
    }

    bool NodeGraph::deserialize(const std::string& jsonStr) {
        size_t pos = 0;
        JsonVal root = parseJson(jsonStr, pos);
        if (root.type != JsonVal::Object) return false;

        clear();
        if (root.has("nextId")) m_nextId = static_cast<uint32_t>(root["nextId"].numVal);

        if (root.has("nodes") && root["nodes"].type == JsonVal::Array) {
            for (const auto& nVal : root["nodes"].arrVal) {
                if (nVal.type != JsonVal::Object) continue;
                Node node;
                node.id         = static_cast<uint32_t>(nVal["id"].numVal);
                node.title      = nVal["title"].strVal;
                node.typeName   = nVal["typeName"].strVal;
                node.position   = ImVec2(static_cast<float>(nVal["x"].numVal),
                                         static_cast<float>(nVal["y"].numVal));
                node.customData = nVal["customData"].strVal;
                if (nVal.has("headerColor"))
                    node.headerColor = static_cast<ImU32>(nVal["headerColor"].numVal);

                auto loadPins = [&](const JsonVal& arr, bool isOutput) {
                    if (arr.type != JsonVal::Array) return;
                    for (const auto& pVal : arr.arrVal) {
                        NodePin pin;
                        pin.id           = static_cast<uint32_t>(pVal["id"].numVal);
                        pin.name         = pVal["name"].strVal;
                        pin.type.name    = pVal["type"].strVal;
                        pin.type.color   = static_cast<ImU32>(pVal["color"].numVal);
                        pin.isOutput     = isOutput;
                        pin.nodeId       = node.id;
                        pin.floatVal     = static_cast<float>(pVal["fVal"].numVal);
                        pin.intVal       = static_cast<int>(pVal["iVal"].numVal);
                        pin.boolVal      = (pVal["bVal"].numVal > 0.5);
                        pin.stringVal    = pVal["sVal"].strVal;
                        if (isOutput) node.outputs.push_back(pin);
                        else          node.inputs.push_back(pin);
                    }
                };
                loadPins(nVal["inputs"],  false);
                loadPins(nVal["outputs"], true);

                // Re-bind callbacks from registered types
                for (const auto& reg : m_registeredTypes) {
                    if (reg.typeName == node.typeName) {
                        node.customWidgetCallback = reg.customWidgetCallback;
                        break;
                    }
                }
                m_nodes.push_back(node);
            }
        }

        if (root.has("links") && root["links"].type == JsonVal::Array) {
            for (const auto& lVal : root["links"].arrVal) {
                if (lVal.type != JsonVal::Object) continue;
                NodeLink link;
                link.id         = static_cast<uint32_t>(lVal["id"].numVal);
                link.fromPinId  = static_cast<uint32_t>(lVal["from"].numVal);
                link.toPinId    = static_cast<uint32_t>(lVal["to"].numVal);
                m_links.push_back(link);
            }
        }
        return true;
    }

    // =========================================================================
    // Drawing — helpers
    // =========================================================================
    static ImVec2 getPinScreenPos(const NodePin& pin, const ImVec2& canvasPos,
                                  const ImVec2& pan, const std::vector<Node>& nodes) {
        for (const auto& node : nodes) {
            if (node.id != pin.nodeId) continue;
            ImVec2 nodeScreenPos = ImVec2(canvasPos.x + pan.x + node.position.x,
                                          canvasPos.y + pan.y + node.position.y);
            float yOffset = 32.0f;
            if (pin.isOutput) {
                int idx = 0;
                for (size_t i = 0; i < node.outputs.size(); ++i)
                    if (node.outputs[i].id == pin.id) { idx = (int)i; break; }
                return ImVec2(nodeScreenPos.x + node.size.x, nodeScreenPos.y + yOffset + idx * 24.0f + 12.0f);
            } else {
                int idx = 0;
                for (size_t i = 0; i < node.inputs.size(); ++i)
                    if (node.inputs[i].id == pin.id) { idx = (int)i; break; }
                return ImVec2(nodeScreenPos.x, nodeScreenPos.y + yOffset + idx * 24.0f + 12.0f);
            }
        }
        return ImVec2(0.0f, 0.0f);
    }

    // =========================================================================
    // Detail Panel
    // =========================================================================
    void NodeGraph::drawDetailPanel(float panelWidth) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(28, 28, 32, 255));
        ImGui::BeginChild("##ng_detail_panel", ImVec2(panelWidth, 0), true);

        // Header
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 180, 200, 255));
        ImGui::TextUnformatted("  Details");
        ImGui::PopStyleColor();
        ImGui::Separator();

        Node* selected = findNode(m_selectedNodeId);
        if (!selected) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(110, 110, 120, 255));
            ImGui::TextWrapped("Select a node to inspect its properties.");
            ImGui::PopStyleColor();
        } else {
            // Node header info
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 215, 80, 255));
            ImGui::TextUnformatted(selected->title.c_str());
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(130, 130, 145, 255));
            ImGui::Text("[%s]", selected->typeName.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::Spacing();

            // Look up registered type for detailPanelCallback
            const NodeRegistryEntry* regEntry = nullptr;
            for (const auto& reg : m_registeredTypes) {
                if (reg.typeName == selected->typeName) {
                    regEntry = &reg;
                    break;
                }
            }

            if (regEntry && regEntry->detailPanelCallback) {
                regEntry->detailPanelCallback(*selected);
            } else {
                // Fallback: show pin list
                if (!selected->inputs.empty()) {
                    ImGui::TextDisabled("Inputs:");
                    for (const auto& p : selected->inputs)
                        ImGui::BulletText("%s  [%s]", p.name.c_str(), p.type.name.c_str());
                    ImGui::Spacing();
                }
                if (!selected->outputs.empty()) {
                    ImGui::TextDisabled("Outputs:");
                    for (const auto& p : selected->outputs)
                        ImGui::BulletText("%s  [%s]", p.name.c_str(), p.type.name.c_str());
                }
                if (selected->inputs.empty() && selected->outputs.empty()) {
                    ImGui::TextDisabled("No properties defined for this node type.");
                }
            }
        }

        ImGui::EndChild();

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH")) {
                if (onDetailPanelAssetDropped && payload->Data) {
                    std::string pathStr(static_cast<const char*>(payload->Data));
                    onDetailPanelAssetDropped(pathStr);
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::PopStyleColor();
    }



    // =========================================================================
    // Canvas draw (extracted for clarity)
    // =========================================================================
    void NodeGraph::drawCanvas(const ImVec2& canvasSize) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 30, 255));

        std::string childId = "##ng_canvas_" + std::string("main");
        ImGui::BeginChild(childId.c_str(), canvasSize, true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);

        ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Grid
        float gridSize  = 64.0f;


        ImU32 gridColor = IM_COL32(42, 42, 42, 255);
        for (float x = std::fmod(m_pan.x, gridSize); x < canvasSize.x; x += gridSize)
            drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y), ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y), gridColor);
        for (float y = std::fmod(m_pan.y, gridSize); y < canvasSize.y; y += gridSize)
            drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y), ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y), gridColor);

        // Pan
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive()) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                m_pan.x += ImGui::GetIO().MouseDelta.x;
                m_pan.y += ImGui::GetIO().MouseDelta.y;
            }
        }

        ImVec2 mousePos = ImGui::GetMousePos();
        uint32_t linkToDelete = 0;

        // Keyboard shortcuts for deleting selected node or link
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                if (m_selectedNodeId != 0) {
                    deleteNode(m_selectedNodeId);
                    m_selectedNodeId = 0;
                } else if (m_selectedLinkId != 0) {
                    removeLink(m_selectedLinkId);
                    m_selectedLinkId = 0;
                }
            }
        }

        // Draw existing links with interactive picking
        for (const auto& link : m_links) {
            NodePin* from = findPin(link.fromPinId);
            NodePin* to   = findPin(link.toPinId);
            if (from && to) {
                ImVec2 p1 = getPinScreenPos(*from, canvasPos, m_pan, m_nodes);
                ImVec2 p2 = getPinScreenPos(*to,   canvasPos, m_pan, m_nodes);
                ImVec2 c1 = ImVec2(p1.x + 60.0f, p1.y);
                ImVec2 c2 = ImVec2(p2.x - 60.0f, p2.y);

                bool isSelected = (m_selectedLinkId == link.id);
                float dist = pointToBezierDistance(mousePos, p1, c1, c2, p2);
                bool isHovered = (dist <= 7.0f);

                if (isHovered && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        m_selectedLinkId = link.id;
                        m_selectedNodeId = 0;
                    }
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        m_selectedLinkId = link.id;
                        ImGui::OpenPopup("ng_link_ctx_menu");
                    }
                }

                ImU32 linkColor = isSelected ? IM_COL32(255, 180, 50, 255) : (isHovered ? IM_COL32(255, 220, 120, 255) : from->type.color);
                float thickness = (isSelected || isHovered) ? 4.5f : 3.0f;
                drawList->AddBezierCubic(p1, c1, c2, p2, linkColor, thickness);
            }
        }

        // Link right-click context menu
        if (ImGui::BeginPopup("ng_link_ctx_menu")) {
            if (ImGui::MenuItem("Delete Link")) {
                if (m_selectedLinkId != 0) {
                    linkToDelete = m_selectedLinkId;
                }
            }
            ImGui::EndPopup();
        }
        if (linkToDelete != 0) {
            removeLink(linkToDelete);
        }

        // Drag preview
        if (m_draggingPinId != 0) {
            NodePin* dragPin = findPin(m_draggingPinId);
            if (dragPin) {
                ImVec2 p1 = getPinScreenPos(*dragPin, canvasPos, m_pan, m_nodes);
                ImVec2 p2 = ImGui::GetMousePos();
                float curveOffset = dragPin->isOutput ? 60.0f : -60.0f;
                drawList->AddBezierCubic(p1, ImVec2(p1.x + curveOffset, p1.y),
                                          ImVec2(p2.x - curveOffset, p2.y), p2,
                                          dragPin->type.color, 3.0f);
            }
        }

        // Draw nodes
        uint32_t nextDragPinId = 0;
        uint32_t nodeToDelete  = 0;


        for (auto& node : m_nodes) {
            ImGui::PushID(node.id);

            ImVec2 nodeScreenPos = ImVec2(canvasPos.x + m_pan.x + node.position.x,
                                          canvasPos.y + m_pan.y + node.position.y);

            float pinsHeight = (float)std::max(node.inputs.size(), node.outputs.size()) * 24.0f;
            float extraHeight = node.customWidgetCallback ? 80.0f : 0.0f;
            node.size.y = 35.0f + pinsHeight + extraHeight + 10.0f;

            // Card header hitbox for moving and selecting node
            ImGui::SetCursorScreenPos(nodeScreenPos);
            ImGui::InvisibleButton("##node_header_hitbox", ImVec2(node.size.x, 28.0f));

            bool isHeaderActive = ImGui::IsItemActive();
            if (isHeaderActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                node.position.x += ImGui::GetIO().MouseDelta.x;
                node.position.y += ImGui::GetIO().MouseDelta.y;
            }

            if (ImGui::IsItemClicked()) {
                uint32_t prevSelected = m_selectedNodeId;
                m_selectedNodeId = node.id;
                m_selectedLinkId = 0;
                if (onNodeSelected && prevSelected != node.id) {
                    onNodeSelected(node.id);
                }
            }

            // Card body background
            if (m_selectedNodeId == node.id) {
                drawList->AddRectFilled(
                    ImVec2(nodeScreenPos.x - 2, nodeScreenPos.y - 2),
                    ImVec2(nodeScreenPos.x + node.size.x + 2, nodeScreenPos.y + node.size.y + 2),
                    IM_COL32(255, 180, 50, 255), 6.0f);
            } else {
                drawList->AddRectFilled(
                    ImVec2(nodeScreenPos.x - 1, nodeScreenPos.y - 1),
                    ImVec2(nodeScreenPos.x + node.size.x + 1, nodeScreenPos.y + node.size.y + 1),
                    IM_COL32(80, 80, 80, 255), 5.0f);
            }
            drawList->AddRectFilled(nodeScreenPos,
                ImVec2(nodeScreenPos.x + node.size.x, nodeScreenPos.y + node.size.y),
                IM_COL32(50, 50, 50, 255), 4.0f);

            // Card body hitbox (allows selecting node by clicking empty body area)
            ImGui::SetCursorScreenPos(ImVec2(nodeScreenPos.x, nodeScreenPos.y + 28.0f));
            ImGui::InvisibleButton("##node_body_hitbox", ImVec2(node.size.x, node.size.y - 28.0f));
            if (ImGui::IsItemClicked()) {
                uint32_t prevSelected = m_selectedNodeId;
                m_selectedNodeId = node.id;
                m_selectedLinkId = 0;
                if (onNodeSelected && prevSelected != node.id) {
                    onNodeSelected(node.id);
                }
            }

            // Header — use per-node color if set, else fall back to type heuristic
            ImU32 headerBg;
            if (node.headerColor != 0) {
                headerBg = node.headerColor;
            } else if (node.typeName.find("Dialogue") != std::string::npos) {
                headerBg = IM_COL32(115, 45, 125, 255);
            } else if (node.typeName.find("Branch") != std::string::npos) {
                headerBg = IM_COL32(125, 75, 35, 255);
            } else {
                headerBg = IM_COL32(35, 75, 125, 255);
            }
            drawList->AddRectFilled(nodeScreenPos,
                ImVec2(nodeScreenPos.x + node.size.x, nodeScreenPos.y + 28.0f), headerBg, 4.0f);
            drawList->AddText(ImVec2(nodeScreenPos.x + 8.0f, nodeScreenPos.y + 6.0f),
                IM_COL32(255, 255, 255, 255), node.title.c_str());

            // Delete button drawn ON TOP of header so it receives clicks cleanly
            if (node.title != "Entry" && node.title != "Any State") {
                ImGui::SetCursorScreenPos(ImVec2(nodeScreenPos.x + node.size.x - 22.0f, nodeScreenPos.y + 4.0f));
                ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(180,  50,  50, 200));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(220,  80,  80, 255));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(130,  30,  30, 255));
                ImGui::PushID(99999 + node.id);
                if (ImGui::Button("x", ImVec2(16.0f, 16.0f)))
                    nodeToDelete = node.id;
                ImGui::PopID();
                ImGui::PopStyleColor(3);
            }

            // Pins (drawn after body hitbox so pin buttons receive mouse events first)
            float yPos = nodeScreenPos.y + 32.0f;

            for (size_t i = 0; i < node.inputs.size(); ++i) {
                auto& pin = node.inputs[i];
                ImVec2 pinCenter = ImVec2(nodeScreenPos.x, yPos + i * 24.0f + 12.0f);
                ImGui::SetCursorScreenPos(ImVec2(pinCenter.x - 10.0f, pinCenter.y - 10.0f));
                ImGui::PushID(pin.id);
                ImGui::InvisibleButton("##pin_in", ImVec2(20.0f, 20.0f));
                bool hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                if (ImGui::IsItemActive() && m_draggingPinId == 0)
                    nextDragPinId = pin.id;
                if (hov && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && m_draggingPinId != 0 && m_draggingPinId != pin.id)
                    addLink(m_draggingPinId, pin.id);
                ImGui::PopID();
                drawList->AddCircleFilled(pinCenter, hov ? 6.5f : 5.0f, pin.type.color);
                drawList->AddCircle(pinCenter, hov ? 6.5f : 5.0f, IM_COL32(0, 0, 0, 200));
                drawList->AddText(ImVec2(pinCenter.x + 12.0f, pinCenter.y - 6.0f),
                    IM_COL32(200, 200, 200, 255), pin.name.c_str());
            }

            for (size_t i = 0; i < node.outputs.size(); ++i) {
                auto& pin = node.outputs[i];
                ImVec2 pinCenter = ImVec2(nodeScreenPos.x + node.size.x, yPos + i * 24.0f + 12.0f);
                ImGui::SetCursorScreenPos(ImVec2(pinCenter.x - 10.0f, pinCenter.y - 10.0f));
                ImGui::PushID(pin.id);
                ImGui::InvisibleButton("##pin_out", ImVec2(20.0f, 20.0f));
                bool hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                if (ImGui::IsItemActive() && m_draggingPinId == 0)
                    nextDragPinId = pin.id;
                if (hov && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && m_draggingPinId != 0 && m_draggingPinId != pin.id)
                    addLink(m_draggingPinId, pin.id);
                ImGui::PopID();
                drawList->AddCircleFilled(pinCenter, hov ? 6.5f : 5.0f, pin.type.color);
                drawList->AddCircle(pinCenter, hov ? 6.5f : 5.0f, IM_COL32(0, 0, 0, 200));
                float labelW = ImGui::CalcTextSize(pin.name.c_str()).x;
                drawList->AddText(ImVec2(pinCenter.x - 12.0f - labelW, pinCenter.y - 6.0f),
                    IM_COL32(200, 200, 200, 255), pin.name.c_str());
            }


            // Inline widget
            if (node.customWidgetCallback) {
                float widgetsY = yPos + pinsHeight + 6.0f;
                ImGui::SetCursorScreenPos(ImVec2(nodeScreenPos.x + 8.0f, widgetsY));
                ImGui::BeginGroup();
                ImGui::PushItemWidth(node.size.x - 16.0f);
                node.customWidgetCallback(node);
                ImGui::PopItemWidth();
                ImGui::EndGroup();
            }

            ImGui::PopID();
        }

        if (nextDragPinId != 0) m_draggingPinId = nextDragPinId;
        if (nodeToDelete  != 0) deleteNode(nodeToDelete);

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_draggingPinId = 0;
        }


        // Right-click context menu
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsAnyItemHovered())
            ImGui::OpenPopup("ng_ctx_menu");


        if (ImGui::BeginPopup("ng_ctx_menu")) {
            ImVec2 mousePos  = ImGui::GetMousePosOnOpeningCurrentPopup();
            ImVec2 spawnPos  = ImVec2(mousePos.x - canvasPos.x - m_pan.x,
                                      mousePos.y - canvasPos.y - m_pan.y);

            std::vector<std::string> categories;
            for (const auto& reg : m_registeredTypes) {
                if (std::find(categories.begin(), categories.end(), reg.category) == categories.end())
                    categories.push_back(reg.category);
            }

            for (const auto& cat : categories) {
                if (ImGui::BeginMenu(cat.c_str())) {
                    for (const auto& reg : m_registeredTypes) {
                        if (reg.category == cat) {
                            if (ImGui::MenuItem(reg.title.c_str())) {
                                uint32_t nodeId = createNode(reg.title, reg.typeName, spawnPos);
                                if (reg.populateCallback)    reg.populateCallback(nodeId);
                                if (Node* n = findNode(nodeId)) {
                                    n->customWidgetCallback = reg.customWidgetCallback;
                                }
                                if (onNodeCreated) onNodeCreated(nodeId, reg.typeName, spawnPos);
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
            }
            ImGui::EndPopup();
        }

        ImGui::EndChild();

        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PAYLOAD_ASSET_PATH");
            if (!payload) payload = ImGui::AcceptDragDropPayload("DND_PAYLOAD_MULTI_ASSETS");
            if (payload && payload->Data && onCanvasAssetDropped) {
                std::string pathStr(static_cast<const char*>(payload->Data));
                size_t pipe = pathStr.find('|');
                if (pipe != std::string::npos) pathStr = pathStr.substr(0, pipe);
                ImVec2 mousePos = ImGui::GetMousePos();
                ImVec2 spawnPos = ImVec2(mousePos.x - canvasPos.x - m_pan.x,
                                          mousePos.y - canvasPos.y - m_pan.y);
                onCanvasAssetDropped(pathStr, spawnPos);
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }



    // =========================================================================
    // Main draw entry point
    // =========================================================================
    void NodeGraph::draw(const char* editorId, const ImVec2& canvasSizeInput, float detailPanelWidth) {
        ImVec2 totalSize = canvasSizeInput;
        if (totalSize.x <= 0.0f) totalSize.x = ImGui::GetContentRegionAvail().x;
        if (totalSize.y <= 0.0f) totalSize.y = ImGui::GetContentRegionAvail().y;

        ImGui::PushID(editorId);

        if (detailPanelWidth > 0.0f) {
            float canvasW = totalSize.x - detailPanelWidth - 4.0f;
            drawCanvas(ImVec2(canvasW, totalSize.y));
            ImGui::SameLine(0.0f, 4.0f);
            drawDetailPanel(detailPanelWidth);
        } else {
            drawCanvas(totalSize);
        }

        ImGui::PopID();
    }

} // namespace Engine
