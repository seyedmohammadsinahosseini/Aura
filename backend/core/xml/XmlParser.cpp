#include "XmlParser.h"

namespace xml {

pugi::xml_document parse(const std::string& xml) {
    pugi::xml_document doc;
    if (xml.empty()) return doc;
    doc.load_string(xml.c_str());
    return doc;
}

pugi::xml_node findNode(const pugi::xml_node& root, const std::string& localName) {
    auto query = std::string("//*[local-name()='") + localName + "']";
    auto result = root.select_node(query.c_str());
    return result.node();
}

std::vector<pugi::xml_node> findNodes(const pugi::xml_node& root, const std::string& localName) {
    auto query = std::string("//*[local-name()='") + localName + "']";
    auto set = root.select_nodes(query.c_str());
    std::vector<pugi::xml_node> nodes;
    for (auto& n : set)
        nodes.push_back(n.node());
    return nodes;
}

std::string text(const pugi::xml_node& root, const std::string& localName) {
    auto node = findNode(root, localName);
    return node.child_value();
}

std::vector<std::string> allText(const pugi::xml_node& root, const std::string& localName) {
    auto nodes = findNodes(root, localName);
    std::vector<std::string> values;
    for (auto& n : nodes)
        values.push_back(n.child_value());
    return values;
}

std::string attr(const pugi::xml_node& node, const std::string& name) {
    return node.attribute(name.c_str()).as_string();
}

std::vector<pugi::xml_node> children(const pugi::xml_node& parent, const std::string& localName) {
    std::vector<pugi::xml_node> result;
    for (auto& child : parent.children()) {
        auto name = child.name();
        auto colon = std::string(name).rfind(':');
        auto base = (colon != std::string::npos) ? name + colon + 1 : name;
        if (base == localName)
            result.push_back(child);
    }
    return result;
}

} // namespace xml
