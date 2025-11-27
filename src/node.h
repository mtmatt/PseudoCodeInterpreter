/// --------------------
/// Node
/// --------------------

#ifndef NODE_H
#define NODE_H

#include <stdexcept>
#include <string>
#include <vector>
#include <memory>
#include "token.h"

const std::string NODE_VALUE{"VALUE"};
const std::string NODE_BINOP{"BINOP"};
const std::string NODE_ERROR{"ERROR"};
const std::string NODE_UNARYOP("UNARYOP");
const std::string NODE_VARASSIGN("VARASSIGN");
const std::string NODE_VARACCESS("VARACCESS");
const std::string NODE_IF("IF");
const std::string NODE_FOR("FOR");
const std::string NODE_WHILE("WHILE");
const std::string NODE_REPEAT("REPEAT");
const std::string NODE_ALGODEF("ALGO");
const std::string NODE_ALGOCALL("CALL");
const std::string NODE_ARRAY("ARRAY");
const std::string NODE_ARRACCESS("ARRACCESS");
const std::string NODE_ARRASSIGN("ARRASSIGN");
const std::string NODE_MEMACCESS("MEMACCESS");
const std::string TAB{"    "};

class Node {
public:
    virtual std::string get_node() = 0;
    virtual ~Node() {};
    virtual std::vector<std::shared_ptr<Node>> get_child() { return std::vector<std::shared_ptr<Node>>(0);}
    virtual std::string get_type() {return "NONE";}
    virtual std::shared_ptr<Token> get_tok() { return nullptr;}
    virtual TokenList get_toks() { return TokenList(0);}
    virtual std::string get_name() { return "";}
};

using NodeList = std::vector<std::shared_ptr<Node>>;

class ErrorNode: public Node {
public:
    ErrorNode(std::shared_ptr<Token> _tok)
        : tok(_tok) {}
    std::string get_node() override;
    std::shared_ptr<Token> get_tok() override { return tok;}
    std::string get_type() override {return NODE_ERROR;}
protected:
    std::shared_ptr<Token> tok;
};

class ValueNode: public Node {
public:
    ValueNode(std::shared_ptr<Token> _tok)
        : tok(_tok) {}
    std::string get_node() override;
    std::string get_type() override {return NODE_VALUE;}
    std::shared_ptr<Token> get_tok() override { return tok;}
protected:
    std::shared_ptr<Token> tok;
};

class BinOpNode: public Node {
public:
    BinOpNode(std::shared_ptr<Node> left, std::shared_ptr<Node> right, std::shared_ptr<Token> tok)
        : left_node(left), right_node(right), op_tok(tok) {}
    std::string get_node() override;
    NodeList get_child() override;
    std::string get_type() override {return NODE_BINOP;}
    std::shared_ptr<Token> get_tok() override { return op_tok;}
protected:
    std::shared_ptr<Node> left_node, right_node;
    std::shared_ptr<Token> op_tok;
};

class UnaryOpNode: public Node {
public:
    UnaryOpNode(std::shared_ptr<Node> _node, std::shared_ptr<Token> tok)
        : node(_node), op_tok(tok) {}
    std::string get_node() override;
    NodeList get_child() override;
    std::string get_type() override { return NODE_UNARYOP;}
    std::shared_ptr<Token> get_tok() override { return op_tok;}
protected:
    std::shared_ptr<Node> node;
    std::shared_ptr<Token> op_tok;
};

class VarAssignNode: public Node {
public:
    VarAssignNode(std::string _name, std::shared_ptr<Node> _node)
        : name(_name), node(_node) {}
    std::string get_node() override;
    NodeList get_child() override { return NodeList{node};}
    std::string get_type() override { return NODE_VARASSIGN;}
    std::shared_ptr<Token> get_tok() override { return nullptr;}
    std::string get_name() override { return name;}
protected:
    std::string name;
    std::shared_ptr<Node> node;
};

class VarAccessNode: public Node {
public:
    VarAccessNode(std::shared_ptr<Token> _tok)
        : tok(_tok) {}
    std::string get_node() override;
    NodeList get_child() override { return NodeList(0);}
    std::string get_type() override{ return NODE_VARACCESS;}
    std::shared_ptr<Token> get_tok() override { return tok;}
    std::string get_name() override {return tok->get_value();}
protected:
    std::shared_ptr<Token> tok;
};

class IfNode: public Node {
public:
    IfNode(std::shared_ptr<Node> condition, NodeList expr, NodeList _else_node)
        : condition_node(condition), expr_node(expr), else_node(_else_node) {}
    std::string get_node() override;
    NodeList get_child() override {
        throw std::runtime_error("should not call get_child on if node");
    }
    const std::shared_ptr<Node>& get_condition() { return condition_node;}
    const NodeList& get_expr() { return expr_node;}
    const NodeList& get_else() { return else_node;}
    std::string get_type() override{ return NODE_IF;}
    std::shared_ptr<Token> get_tok() override { return nullptr;}
    std::string get_name() override { return "";}
protected:
    std::shared_ptr<Node> condition_node; 
    NodeList expr_node, else_node;
};

class ForNode: public Node {
public:
    ForNode(
        std::shared_ptr<Node> _var_assign, std::shared_ptr<Node> _end_value,
        std::shared_ptr<Node> _step_value, NodeList _body_node
    )   : var_assign(_var_assign), end_value(_end_value), step_value(_step_value), body_node(_body_node) {}
    std::string get_node() override;
    NodeList get_child() override {
        NodeList child{var_assign, end_value, step_value};
        for(auto node : body_node) child.push_back(node);
        return child;
    }
    std::string get_type() override { return NODE_FOR;}
    std::shared_ptr<Token> get_tok() override { return nullptr;}
    std::string get_name() override { return "";}
protected:
    std::shared_ptr<Node> var_assign, end_value, step_value;
    NodeList body_node;
};

class WhileNode: public Node {
public:
    WhileNode(std::shared_ptr<Node> _condition, NodeList _body_node)
        : condition(_condition), body_node(_body_node) {}
    std::string get_node() override;
    NodeList get_child() override {
        NodeList child{condition};
        for(auto node : body_node) child.push_back(node);
        return child;
    }
    std::string get_type() override { return NODE_WHILE;}
    std::shared_ptr<Token> get_tok() override { return nullptr;}
    std::string get_name() override { return "";}
protected:
    std::shared_ptr<Node> condition;
    NodeList body_node;
};

class RepeatNode: public Node {
public:
    RepeatNode(NodeList _body_node, std::shared_ptr<Node> _condition)
        : condition(_condition), body_node(_body_node) {}
    std::string get_node() override;
    NodeList get_child() override {
        NodeList child{condition};
        for(auto node : body_node) child.push_back(node);
        return child;
    }
    std::string get_type() override { return NODE_REPEAT;}
    std::shared_ptr<Token> get_tok() override { return nullptr;}
    std::string get_name() override { return "";}
protected:
    std::shared_ptr<Node> condition;
    NodeList body_node;
};

class AlgorithmDefNode: public Node {
public:
    AlgorithmDefNode(std::shared_ptr<Token> _algo_name, const TokenList &_args_name, NodeList _body_node = {})
        : algo_name(_algo_name), args_name(_args_name), body_node(_body_node) {}
    std::string get_node() override;
    NodeList get_child() override { return body_node;}
    std::string get_type() override { return NODE_ALGODEF;}
    std::shared_ptr<Token> get_tok() override { return algo_name;}
    TokenList get_toks() override { return args_name;}
    std::string get_name() override { return algo_name->get_value();}
protected:
    std::shared_ptr<Token> algo_name;
    TokenList args_name;
    NodeList body_node;
};

class AlgorithmCallNode: public Node {
public:
    AlgorithmCallNode(std::shared_ptr<Node> _call_node, const NodeList &_args)
        : call_node(_call_node), args(_args) {}
    std::string get_node() override;
    NodeList get_child() override { return args;}
    std::string get_type() override { return NODE_ALGOCALL;}
    std::shared_ptr<Token> get_tok() override { return nullptr;}
    std::string get_name() override { return call_node->get_name();}
    std::shared_ptr<Node> get_call() { return call_node;}
protected:
    std::shared_ptr<Node> call_node;
    NodeList args;
};

class ArrayNode: public Node {
public:
    ArrayNode(const NodeList &_elements_node)
        : elements_node(_elements_node) {}
    std::string get_node() override;
    NodeList get_child() override { return elements_node;}
    std::string get_type() override { return NODE_ARRAY;}
    std::shared_ptr<Token> get_tok() override { return nullptr;}
    std::string get_name() override { return "";}
protected:
    NodeList elements_node;
};

class ArrayAccessNode: public Node {
public:
    ArrayAccessNode(std::shared_ptr<Node> _arr, std::shared_ptr<Node> _index)
        : arr(_arr), index(_index) {}
    std::string get_node() override;
    NodeList get_child() override { return NodeList{arr, index};}
    std::string get_type() override { return NODE_ARRACCESS;}
    std::shared_ptr<Token> get_tok() override { return nullptr;}
protected:
    std::shared_ptr<Node> arr, index;
};

class ArrayAssignNode: public Node {
public:
    ArrayAssignNode(std::shared_ptr<Node> _arr, std::shared_ptr<Node> _value)
        : arr(_arr), value(_value) {}
    std::string get_node() override;
    NodeList get_child() override { return NodeList{arr, value};}
    std::string get_type() override { return NODE_ARRASSIGN;}
    std::shared_ptr<Token> get_tok() override { return nullptr;}
protected:
    std::shared_ptr<Node> arr, value;
};

class MemberAccessNode: public Node {
public:
    MemberAccessNode(std::shared_ptr<Node> _obj, std::shared_ptr<Node> _member)
        : obj(_obj), member(_member) {}
    std::string get_node() override;
    NodeList get_child() override { return NodeList{obj, member};}
    std::string get_type() override { return NODE_MEMACCESS;}
    std::shared_ptr<Token> get_tok() override { return nullptr;}
protected:
    std::shared_ptr<Node> obj, member;
};

#endif