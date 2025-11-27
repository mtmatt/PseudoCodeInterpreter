/// --------------------
/// Node
/// --------------------

#include "node.h"
#include <string>
#include <iostream>
#include <sstream>

std::string ValueNode::get_node() {
    return tok->get_tok();
}

std::string ErrorNode::get_node() {
    return tok->get_tok();
}

std::string BinOpNode::get_node() {
    std::stringstream ss;
    ss << "(" << left_node->get_node() << ", " << op_tok->get_tok() << ", " << right_node->get_node() << ")";
    std::string ret;
    std::getline(ss, ret);
    return ret;
}

NodeList BinOpNode::get_child() {
    return NodeList{left_node, right_node};
}

std::string UnaryOpNode::get_node() {
    std::stringstream ss;
    ss << "(" << op_tok->get_tok() << ", " << node->get_node() << ")";
    std::string ret;
    std::getline(ss, ret);
    return ret;
}

NodeList UnaryOpNode::get_child() {
    return NodeList{node};
}

std::string VarAssignNode::get_node() {
    std::stringstream ss;
    ss << "(VAR " << name << " <- " << node->get_node() << ")";
    std::string ret;
    std::getline(ss, ret);
    return ret;
}

std::string VarAccessNode::get_node() {
    return "(ACCESS: " + tok->get_tok() + ")";
}

std::string IfNode::get_node() {
    std::stringstream ss;
    ss << "(IF " << condition_node->get_node() << " THEN ";
    for(auto node : expr_node)
        ss << node->get_node() << "; ";
    ss << "\nELSE ";
    for(auto node : else_node)
        ss << node->get_node() << "; ";
    ss << ")";
    std::string ret;
    std::getline(ss, ret);
    return ret;
}

std::string ForNode::get_node() {
    std::stringstream ss;
    ss << "(FOR " << var_assign->get_node() << " TO " << end_value->get_node();
    if(step_value != nullptr)
        ss << " STEP " << step_value->get_node();
    ss << " DO ";
    for(auto node : body_node)
        ss << node->get_node() << "; ";
    ss << ")";
    std::string ret;
    std::getline(ss, ret);
    return ret;
}

std::string WhileNode::get_node() {
    std::stringstream ss;
    ss << "(WHILE " << condition->get_node() << " DO ";
    for(auto node : body_node)
        ss << node->get_node() << "; ";
    ss << ")";
    std::string ret;
    std::getline(ss, ret);
    return ret;
}

std::string RepeatNode::get_node() {
    std::stringstream ss;
    ss << "(REPEAT ";
    for(auto node : body_node)
        ss << node->get_node() << "; ";
    ss << " UNTIL " << condition->get_node() << ")";
    std::string ret;
    std::getline(ss, ret);
    return ret;
}

std::string AlgorithmDefNode::get_node() {
    std::stringstream ss;
    ss << "ALGORITHM " << algo_name->get_tok() << "(";
    if(!args_name.empty()) {
        ss << args_name[0]->get_tok();
    }
    for(int i{1}; i < args_name.size(); ++i) {
        ss << ", " << args_name[i]->get_tok();
    }
    ss << "):\n";
    for(auto exp : body_node) {
        ss << TAB << exp->get_node() << "\n";
    }
    std::string ret, line;
    while(std::getline(ss, line)) {
        ret += line;
        ret += "\n";
    }
    return ret;
}

std::string AlgorithmCallNode::get_node() {
    std::stringstream ss;
    ss << "(CALL ALGORITHM " << call_node->get_name() << "(";
    if(!args.empty()) {
        ss << args[0]->get_node();
    }
    for(int i{1}; i < args.size(); ++i) {
        ss << ", " << args[i]->get_node();
    }
    ss << "))";
    std::string ret;
    std::getline(ss, ret);
    return ret;
}

std::string ArrayNode::get_node() {
    std::stringstream ss;
    ss << "{";
    if(!elements_node.empty()) {
        ss << elements_node[0]->get_node();
    }
    for(int i{1}; i < elements_node.size(); ++i) {
        ss << ", " << elements_node[i]->get_node();
    }
    ss << "}";
    std::string ret;
    std::getline(ss, ret);
    return ret;
}

std::string ArrayAccessNode::get_node() {
    std::stringstream ss;
    ss << arr->get_node() << "[" << index->get_node() << "]";
    std::string ret;
    std::getline(ss, ret);
    return ret;
}

std::string ArrayAssignNode::get_node() {
    std::stringstream ss;
    ss << arr->get_node() << " <- " << value->get_node();
    std::string ret;
    std::getline(ss, ret);
    return ret;
}

std::string MemberAccessNode::get_node() {
    std::stringstream ss;
    ss << obj->get_node() << "." << member->get_node();
    std::string ret;
    std::getline(ss, ret);
    return ret;
}