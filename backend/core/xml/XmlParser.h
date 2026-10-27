#ifndef XML_PARSER_H
#define XML_PARSER_H

#include <pugixml.hpp>
#include <string>
#include <vector>

namespace xml {

pugi::xml_document parse(const std::string& xml);

pugi::xml_node findNode(const pugi::xml_node& root, const std::string& localName);
std::vector<pugi::xml_node> findNodes(const pugi::xml_node& root, const std::string& localName);

std::string text(const pugi::xml_node& root, const std::string& localName);
std::vector<std::string> allText(const pugi::xml_node& root, const std::string& localName);

std::string attr(const pugi::xml_node& node, const std::string& name);

std::vector<pugi::xml_node> children(const pugi::xml_node& parent, const std::string& localName);

} // namespace xml

#endif // XML_PARSER_H
