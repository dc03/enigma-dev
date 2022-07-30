#include <parsing/ast.h>
#include <parsing/parser.cpp>
#include <languages/lang_CPP.h>

#include <JDI/src/System/builtins.h>
#include <gtest/gtest.h>

using namespace ::enigma::parsing;

class TestFailureErrorHandler : public ErrorHandler {
 public:
  void ReportError(CodeSnippet snippet, std::string_view error) final {
    ADD_FAILURE() << "Test reported an error at line " << snippet.line << ", position " << snippet.position << ": "
                  << error;
  }
  void ReportWarning(CodeSnippet snippet, std::string_view warning) final {
    ADD_FAILURE() << "Test reported a warning at line " << snippet.line << ", position " << snippet.position << ": "
                  << warning;
  }
};

struct ParserTester {
  TestFailureErrorHandler herr;
  const ParseContext *context;
  Lexer lexer;
  lang_CPP cpp{};
  AstBuilder builder;

  AstBuilder *operator->() { return &builder; }

  ParserTester(std::string code, bool use_cpp = false)
      : context(&ParseContext::ForTesting(use_cpp)),
        lexer(std::move(code), context, &herr),
        builder{&lexer, &herr, SyntaxMode::STRICT, &cpp} {}
};

TEST(ParserTest, Basics) {
  ParserTester test{"(x ? y : z ? a : (z[5](6)))"};

  auto node = test->TryParseExpression(Precedence::kAll);
  ASSERT_EQ(node->type, AST::NodeType::PARENTHETICAL);

  auto *expr = dynamic_cast<AST::Parenthetical *>(node.get())->expression.get();
  ASSERT_EQ(expr->type, AST::NodeType::TERNARY_EXPRESSION);

  auto *ternary = dynamic_cast<AST::TernaryExpression *>(expr);
  auto *cond = ternary->condition.get();
  auto *true_ = ternary->true_expression.get();
  auto *false_ = ternary->false_expression.get();
  ASSERT_EQ(cond->type, AST::NodeType::LITERAL);
  ASSERT_EQ(std::get<std::string>(dynamic_cast<AST::Literal *>(cond)->value.value), "x");

  ASSERT_EQ(true_->type, AST::NodeType::LITERAL);
  ASSERT_EQ(std::get<std::string>(dynamic_cast<AST::Literal *>(true_)->value.value), "y");

  ASSERT_EQ(false_->type, AST::NodeType::TERNARY_EXPRESSION);

  ternary = dynamic_cast<AST::TernaryExpression *>(false_);
  cond = ternary->condition.get();
  true_ = ternary->true_expression.get();
  false_ = ternary->false_expression.get();

  ASSERT_EQ(cond->type, AST::NodeType::LITERAL);
  ASSERT_EQ(std::get<std::string>(dynamic_cast<AST::Literal *>(cond)->value.value), "z");

  ASSERT_EQ(true_->type, AST::NodeType::LITERAL);
  ASSERT_EQ(std::get<std::string>(dynamic_cast<AST::Literal *>(true_)->value.value), "a");

  ASSERT_EQ(false_->type, AST::NodeType::PARENTHETICAL);
  expr = dynamic_cast<AST::Parenthetical *>(false_)->expression.get();
  ASSERT_EQ(expr->type, AST::NodeType::FUNCTION_CALL);
  auto *function = dynamic_cast<AST::FunctionCallExpression *>(expr);
  auto *called = function->function.get();
  auto *args = &function->arguments;

  ASSERT_EQ(called->type, AST::NodeType::BINARY_EXPRESSION);

  auto *bin = dynamic_cast<AST::BinaryExpression*>(called);
  ASSERT_EQ(bin->operation, TokenType::TT_BEGINBRACKET);
  ASSERT_EQ(bin->left->type, AST::NodeType::LITERAL);
  auto *left = dynamic_cast<AST::Literal*>(bin->left.get());
  ASSERT_EQ(std::get<std::string>(dynamic_cast<AST::Literal *>(left)->value.value), "z");

  ASSERT_EQ(bin->right->type, AST::NodeType::LITERAL);
  auto *right = dynamic_cast<AST::Literal*>(bin->right.get());
  ASSERT_EQ(std::get<std::string>(dynamic_cast<AST::Literal *>(right)->value.value), "5");

  ASSERT_EQ(args->size(), 1);
  auto *arg = (*args)[0].get();
  ASSERT_EQ(arg->type, AST::NodeType::LITERAL);
  ASSERT_EQ(std::get<std::string>(dynamic_cast<AST::Literal *>(arg)->value.value), "6");
}

TEST(ParserTest, SizeofExpression) {
  ParserTester test{"sizeof 5"};
  auto expr = test->TryParseExpression(Precedence::kAll);

  ASSERT_EQ(expr->type, AST::NodeType::SIZEOF);
  auto *sizeof_ = dynamic_cast<AST::SizeofExpression *>(expr.get());
  ASSERT_EQ(sizeof_->kind, AST::SizeofExpression::Kind::EXPR);
  ASSERT_TRUE(std::holds_alternative<AST::PNode>(sizeof_->argument));

  auto &value = std::get<AST::PNode>(sizeof_->argument);
  ASSERT_EQ(value->type, AST::NodeType::LITERAL);
  auto *literal = dynamic_cast<AST::Literal *>(value.get());
  ASSERT_EQ(std::get<std::string>(literal->value.value), "5");
}

TEST(ParserTest, SizeofVariadic) {
  ParserTester test{"sizeof...(ident)"};
  auto expr = test->TryParseExpression(Precedence::kAll);

  ASSERT_EQ(expr->type, AST::NodeType::SIZEOF);
  auto *sizeof_ = dynamic_cast<AST::SizeofExpression *>(expr.get());
  ASSERT_EQ(sizeof_->kind, AST::SizeofExpression::Kind::VARIADIC);
  ASSERT_TRUE(std::holds_alternative<std::string>(sizeof_->argument));

  auto &value = std::get<std::string>(sizeof_->argument);
  ASSERT_EQ(value, "ident");
}

#include <bitset>

TEST(ParserTest, SizeofType) {
  ParserTester test{"sizeof(const volatile unsigned long long int **(*)[10])"};
  auto expr = test->TryParseExpression(Precedence::kAll);

  ASSERT_EQ(expr->type, AST::NodeType::SIZEOF);
  auto *sizeof_ = dynamic_cast<AST::SizeofExpression *>(expr.get());
  ASSERT_EQ(sizeof_->kind, AST::SizeofExpression::Kind::TYPE);
  ASSERT_TRUE(std::holds_alternative<FullType>(sizeof_->argument));

  auto &value = std::get<FullType>(sizeof_->argument);
  auto has_value = [&value](jdi::typeflag *builtin) -> bool {
    return (value.flags & builtin->mask) == builtin->value;
  };
  ASSERT_TRUE(has_value(jdi::builtin_flag__const));
  ASSERT_TRUE(has_value(jdi::builtin_flag__volatile));
  ASSERT_TRUE(has_value(jdi::builtin_flag__unsigned));
  ASSERT_TRUE(has_value(jdi::builtin_flag__long_long));
  ASSERT_EQ(value.def->flags & jdi::DEF_TYPENAME, jdi::DEF_TYPENAME);
  ASSERT_EQ(value.def->name, "int");
  ASSERT_EQ(value.decl.components.size(), 3);
  jdi::ref_stack stack;
  value.decl.to_jdi_refstack(stack);
  auto first = stack.begin();
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_POINTERTO);
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_ARRAYBOUND);
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_POINTERTO);
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_POINTERTO);
}

TEST(ParserTest, AlignofType) {
  ParserTester test{"alignof(const volatile unsigned long long *)"};
  auto expr = test->TryParseExpression(Precedence::kAll);

  ASSERT_EQ(expr->type, AST::NodeType::ALIGNOF);
  auto *alignof_ = dynamic_cast<AST::AlignofExpression *>(expr.get());
  auto &value = alignof_->type;
  auto has_value = [&value](jdi::typeflag *builtin) -> bool {
    return (value.flags & builtin->mask) == builtin->value;
  };
  ASSERT_TRUE(has_value(jdi::builtin_flag__const));
  ASSERT_TRUE(has_value(jdi::builtin_flag__volatile));
  ASSERT_TRUE(has_value(jdi::builtin_flag__unsigned));
  ASSERT_TRUE(has_value(jdi::builtin_flag__long_long));
  ASSERT_EQ(value.def->flags & jdi::DEF_TYPENAME, jdi::DEF_TYPENAME);
  ASSERT_EQ(value.def->name, "int");
  ASSERT_EQ(value.decl.components.size(), 1);
  jdi::ref_stack stack;
  value.decl.to_jdi_refstack(stack);
  auto first = stack.begin();
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_POINTERTO);
}

bool contains_flag(FullType *ft, std::size_t decflag) {
  return (ft->flags & decflag) == decflag;
}

bool def_type_is(FullType *ft, std::size_t dectype) {
  return (ft->def->flags & dectype) == dectype;
}

TEST(ParserTest, TypeSpecifierAndDeclarator) {
  ParserTester test{"const unsigned int ****(***)[10]"};
  FullType ft = test->TryParseTypeID();
  EXPECT_TRUE(def_type_is(&ft, jdi::DEF_TYPENAME));
  EXPECT_TRUE(contains_flag(&ft, jdi::builtin_flag__const->value));
  EXPECT_TRUE(contains_flag(&ft, jdi::builtin_flag__unsigned->value));
  jdi::ref_stack stack;
  ft.decl.to_jdi_refstack(stack);
  auto first = stack.begin();
  EXPECT_EQ((first++)->type, jdi::ref_stack::RT_POINTERTO);
  EXPECT_EQ((first++)->type, jdi::ref_stack::RT_POINTERTO);
  EXPECT_EQ((first++)->type, jdi::ref_stack::RT_POINTERTO);
  EXPECT_EQ((first++)->type, jdi::ref_stack::RT_ARRAYBOUND);
  EXPECT_EQ((first++)->type, jdi::ref_stack::RT_POINTERTO);
  EXPECT_EQ((first++)->type, jdi::ref_stack::RT_POINTERTO);
  EXPECT_EQ((first++)->type, jdi::ref_stack::RT_POINTERTO);
  EXPECT_EQ((first++)->type, jdi::ref_stack::RT_POINTERTO);
  EXPECT_EQ(test.lexer.ReadToken().type, TT_ENDOFCODE);
}

TEST(ParserTest, Declarator_1) {
  FullType ft2;
  ParserTester test2{"const unsigned int **(*var::*y)[10]"};
  test2->TryParseTypeSpecifierSeq(&ft2);
  test2->TryParseDeclarator(&ft2, AST::DeclaratorType::NON_ABSTRACT);

  jdi::ref_stack stack;
  ft2.decl.to_jdi_refstack(stack);
  auto first = stack.begin();
  EXPECT_EQ(ft2.decl.name, "y");
  EXPECT_EQ((first++)->type, jdi::ref_stack::RT_POINTERTO);
  EXPECT_EQ((first++)->type, jdi::ref_stack::RT_MEMBER_POINTER);
  EXPECT_EQ((first++)->type, jdi::ref_stack::RT_ARRAYBOUND);
  EXPECT_EQ((first++)->type, jdi::ref_stack::RT_POINTERTO);
  EXPECT_EQ((first++)->type, jdi::ref_stack::RT_POINTERTO);
  EXPECT_EQ(test2.lexer.ReadToken().type, TT_ENDOFCODE);
}

TEST(ParserTest, Declarator_2) {
  FullType ft3;
  ParserTester test3{"int ((*a)(int (*x)(int x), int (*)[10]))(int)"};
  test3->TryParseTypeSpecifierSeq(&ft3);
  test3->TryParseDeclarator(&ft3);

  EXPECT_EQ(test3.lexer.ReadToken().type, TT_ENDOFCODE);
}

TEST(ParserTest, Declarator_3) {
  ParserTester test{"int *(*(*a)[10][12])[15]"};
  auto node = test->TryParseDeclarations();
  ASSERT_EQ(node->type, AST::NodeType::DECLARATION);
  auto *decls = dynamic_cast<AST::DeclarationStatement *>(node.get());
  ASSERT_EQ(decls->declarations.size(), 1);

  ASSERT_EQ(decls->declarations[0].init, nullptr);
  auto &decl1 = decls->declarations[0].declarator.decl;
  ASSERT_EQ(decl1.name, "a");
  ASSERT_EQ(decl1.components.size(), 2);

  ASSERT_EQ(decl1.components[0].kind, DeclaratorNode::Kind::POINTER_TO);
  auto &ptr = decl1.components[0].as<PointerNode>();
  ASSERT_EQ(ptr.is_const, false);
  ASSERT_EQ(ptr.is_volatile, false);
  ASSERT_EQ(ptr.class_def, nullptr);

  ASSERT_EQ(decl1.components[1].kind, DeclaratorNode::Kind::NESTED);
  auto *nested = decl1.components[1].as<NestedNode>().contained.get();
  ASSERT_EQ(nested->components.size(), 3);

  ASSERT_EQ(nested->components[0].kind, DeclaratorNode::Kind::POINTER_TO);
  auto &nested_ptr = nested->components[0].as<PointerNode>();
  ASSERT_EQ(nested_ptr.is_const, false);
  ASSERT_EQ(nested_ptr.is_volatile, false);
  ASSERT_EQ(nested_ptr.class_def, nullptr);

  ASSERT_EQ(nested->components[1].kind, DeclaratorNode::Kind::NESTED);
  auto *nested_nested = nested->components[1].as<NestedNode>().contained.get();
  ASSERT_EQ(nested_nested->components.size(), 3);
  ASSERT_EQ(nested_nested->components[0].kind, DeclaratorNode::Kind::POINTER_TO);
  auto &nested_nested_ptr = nested_nested->components[0].as<PointerNode>();
  ASSERT_EQ(nested_nested_ptr.is_const, false);
  ASSERT_EQ(nested_nested_ptr.is_volatile, false);
  ASSERT_EQ(nested_nested_ptr.class_def, nullptr);
  ASSERT_EQ(nested_nested->components[1].kind, DeclaratorNode::Kind::ARRAY_BOUND);
  ASSERT_EQ(nested_nested->components[2].kind, DeclaratorNode::Kind::ARRAY_BOUND);

  ASSERT_EQ(nested->components[2].kind, DeclaratorNode::Kind::ARRAY_BOUND);
}

TEST(ParserTest, Declarator_4) {
  ParserTester test{"int *(*(*a)[10][12])[15]"};
  auto node = test->TryParseDeclarations();
  ASSERT_EQ(node->type, AST::NodeType::DECLARATION);
  auto *decls = dynamic_cast<AST::DeclarationStatement *>(node.get());
  ASSERT_EQ(decls->declarations.size(), 1);

  ASSERT_EQ(decls->declarations[0].init, nullptr);
  auto &decl1 = decls->declarations[0].declarator.decl;
  ASSERT_EQ(decl1.name, "a");
  ASSERT_EQ(decl1.components.size(), 2);

  jdi::ref_stack stack;
  decl1.to_jdi_refstack(stack);
  auto first = stack.begin();
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_POINTERTO);
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_ARRAYBOUND);
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_ARRAYBOUND);
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_POINTERTO);
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_ARRAYBOUND);
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_POINTERTO);
}

TEST(ParserTest, Declaration) {
  ParserTester test{"const unsigned *(*x)[10] = nullptr;"};
  FullType ft;
  test->TryParseTypeSpecifierSeq(&ft);
  test->TryParseDeclarator(&ft);
  test->TryParseInitializer();
  EXPECT_EQ(test->current_token().type, TT_SEMICOLON);
  EXPECT_EQ(test.lexer.ReadToken().type, TT_ENDOFCODE);
}

TEST(ParserTest, Declarations) {
  ParserTester test{"int *x = nullptr, y, (*z)(int x, int) = &y;"};

  auto node = test->TryParseDeclarations();
  EXPECT_EQ(test->current_token().type, TT_SEMICOLON);
  EXPECT_EQ(test.lexer.ReadToken().type, TT_ENDOFCODE);

  ASSERT_EQ(node->type, AST::NodeType::DECLARATION);
  auto *decls = reinterpret_cast<AST::DeclarationStatement *>(node.get());
  EXPECT_EQ(decls->type->flags & jdi::DEF_TYPENAME, jdi::DEF_TYPENAME);

  EXPECT_EQ(decls->declarations.size(), 3);
  EXPECT_NE(decls->declarations[0].init, nullptr);
  EXPECT_EQ(decls->declarations[0].declarator.def->flags & jdi::DEF_TYPENAME, jdi::DEF_TYPENAME);
  EXPECT_EQ(decls->declarations[0].declarator.decl.components.begin()->kind, DeclaratorNode::Kind::POINTER_TO);

  EXPECT_EQ(decls->declarations[1].init, nullptr);
  EXPECT_EQ(decls->declarations[1].declarator.def->flags & jdi::DEF_TYPENAME, jdi::DEF_TYPENAME);
  EXPECT_EQ(decls->declarations[1].declarator.decl.components.size(), 0);

  EXPECT_NE(decls->declarations[2].init, nullptr);
  EXPECT_EQ(decls->declarations[2].declarator.def->flags & jdi::DEF_TYPENAME, jdi::DEF_TYPENAME);
  EXPECT_EQ(decls->declarations[2].declarator.decl.components.size(), 1);
}

void check_placement(AST::NewExpression *new_) {
  ASSERT_NE(new_->placement, nullptr);
  auto *placement = new_->placement.get();
  ASSERT_EQ(placement->kind, AST::Initializer::Kind::PLACEMENT_NEW);
  ASSERT_TRUE(std::holds_alternative<AST::BraceOrParenInitNode>(placement->initializer));
  auto *placement_args = std::get<AST::BraceOrParenInitNode>(placement->initializer).get();
  ASSERT_EQ(placement_args->kind, AST::BraceOrParenInitializer::Kind::PAREN_INIT);
  ASSERT_EQ(placement_args->values.size(), 1);
  ASSERT_EQ(placement_args->values[0].second->kind, AST::Initializer::Kind::ASSIGN_EXPR);
  ASSERT_TRUE(std::holds_alternative<AST::AssignmentInitNode>(placement_args->values[0].second->initializer));
  auto *placement_arg = std::get<AST::AssignmentInitNode>(placement_args->values[0].second->initializer).get();
  ASSERT_EQ(placement_arg->kind, AST::AssignmentInitializer::Kind::EXPR);
  auto *placement_expr = std::get<std::unique_ptr<AST::Node>>(placement_arg->initializer).get();
  ASSERT_EQ(placement_expr->type, AST::NodeType::LITERAL);
  ASSERT_EQ(std::get<std::string>(dynamic_cast<AST::Literal *>(placement_expr)->value.value), "nullptr");
}

void check_initializer(AST::NewExpression *new_, AST::BraceOrParenInitializer::Kind kind) {
  ASSERT_NE(new_->initializer, nullptr);
  auto *init = new_->initializer.get();
  ASSERT_EQ(init->kind, AST::Initializer::Kind::BRACE_INIT);
  ASSERT_TRUE(std::holds_alternative<AST::BraceOrParenInitNode>(init->initializer));
  auto *brace = std::get<AST::BraceOrParenInitNode>(init->initializer).get();
  ASSERT_EQ(brace->kind, kind);
  ASSERT_EQ(brace->values.size(), 5);
  for (int i = 0; i < 5; i++) {
    ASSERT_EQ(brace->values[i].first, "");
    ASSERT_EQ(brace->values[i].second->kind, AST::Initializer::Kind::ASSIGN_EXPR);
    ASSERT_TRUE(std::holds_alternative<AST::AssignmentInitNode>(brace->values[i].second->initializer));
    auto *assign = std::get<AST::AssignmentInitNode>(brace->values[i].second->initializer).get();
    ASSERT_EQ(assign->kind, AST::AssignmentInitializer::Kind::EXPR);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<AST::Node>>(assign->initializer));
    auto *expr = std::get<std::unique_ptr<AST::Node>>(assign->initializer).get();
    ASSERT_EQ(expr->type, AST::NodeType::LITERAL);
    ASSERT_EQ(std::get<std::string>(dynamic_cast<AST::Literal *>(expr)->value.value), std::to_string(i + 1));
  }
}

TEST(ParserTest, NewExpression_1) {
  ParserTester test{"new (nullptr) int[]{1, 2, 3, 4, 5};"};
  auto node = test->TryParseExpression(Precedence::kAll);
  ASSERT_EQ(test->current_token().type, TT_SEMICOLON);
  ASSERT_EQ(test.lexer.ReadToken().type, TT_ENDOFCODE);

  ASSERT_EQ(node->type, AST::NodeType::NEW);
  auto *new_ = reinterpret_cast<AST::NewExpression *>(node.get());
  ASSERT_FALSE(new_->is_global);
  ASSERT_TRUE(new_->is_array);

  check_placement(new_);

  EXPECT_EQ(new_->type.def, jdi::builtin_type__int);
  ASSERT_EQ(new_->type.decl.components.size(), 1);
  ASSERT_EQ(new_->type.decl.components.begin()->kind, DeclaratorNode::Kind::ARRAY_BOUND);

  check_initializer(new_, AST::BraceOrParenInitializer::Kind::BRACE_INIT);
}

TEST(ParserTest, NewExpression_2) {
  ParserTester test{"::new int[][15]{1, 2, 3, 4, 5};"};
  auto node = test->TryParseExpression(Precedence::kAll);
  ASSERT_EQ(test->current_token().type, TT_SEMICOLON);
  ASSERT_EQ(test.lexer.ReadToken().type, TT_ENDOFCODE);

  ASSERT_EQ(node->type, AST::NodeType::NEW);
  auto *new_ = reinterpret_cast<AST::NewExpression *>(node.get());
  ASSERT_TRUE(new_->is_global);
  ASSERT_TRUE(new_->is_array);

  ASSERT_EQ(new_->placement, nullptr);
  EXPECT_EQ(new_->type.def, jdi::builtin_type__int);
  ASSERT_EQ(new_->type.decl.components.size(), 2);
  jdi::ref_stack stack;
  new_->type.decl.to_jdi_refstack(stack);
  auto first = stack.begin();
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_ARRAYBOUND);
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_ARRAYBOUND);

  check_initializer(new_, AST::BraceOrParenInitializer::Kind::BRACE_INIT);
}

TEST(ParserTest, NewExpression_3) {
  ParserTester test{"::new (nullptr) (int *(**)[10])(1, 2, 3, 4, 5);"};
  auto node = test->TryParseExpression(Precedence::kAll);
  ASSERT_EQ(test->current_token().type, TT_SEMICOLON);
  ASSERT_EQ(test.lexer.ReadToken().type, TT_ENDOFCODE);

  ASSERT_EQ(node->type, AST::NodeType::NEW);
  auto *new_ = reinterpret_cast<AST::NewExpression *>(node.get());
  ASSERT_TRUE(new_->is_global);
  ASSERT_FALSE(new_->is_array);

  check_placement(new_);

  ASSERT_EQ(new_->type.def, jdi::builtin_type__int);
  ASSERT_EQ(new_->type.decl.components.size(), 2);
  jdi::ref_stack stack;
  new_->type.decl.to_jdi_refstack(stack);
  auto first = stack.begin();
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_POINTERTO);
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_POINTERTO);
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_ARRAYBOUND);
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_POINTERTO);

  check_initializer(new_, AST::BraceOrParenInitializer::Kind::PAREN_INIT);
}

TEST(ParserTest, NewExpression_4) {
  ParserTester test{"new (int *(**)[10]);"};
  auto node = test->TryParseExpression(Precedence::kAll);
  ASSERT_EQ(test->current_token().type, TT_SEMICOLON);
  ASSERT_EQ(test.lexer.ReadToken().type, TT_ENDOFCODE);

  ASSERT_EQ(node->type, AST::NodeType::NEW);
  auto *new_ = reinterpret_cast<AST::NewExpression *>(node.get());
  ASSERT_FALSE(new_->is_global);
  ASSERT_FALSE(new_->is_array);

  ASSERT_EQ(new_->placement, nullptr);
  ASSERT_EQ(new_->type.def, jdi::builtin_type__int);
  ASSERT_EQ(new_->type.decl.components.size(), 2);
  jdi::ref_stack stack;
  new_->type.decl.to_jdi_refstack(stack);
  auto first = stack.begin();
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_POINTERTO);
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_POINTERTO);
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_ARRAYBOUND);
  ASSERT_EQ(first++->type, jdi::ref_stack::RT_POINTERTO);
}

TEST(ParserTest, DeleteExpression_1) {
  ParserTester test{"delete x;"};
  auto node = test->TryParseExpression(Precedence::kAll);
  ASSERT_EQ(test->current_token().type, TT_SEMICOLON);
  ASSERT_EQ(test.lexer.ReadToken().type, TT_ENDOFCODE);

  ASSERT_EQ(node->type, AST::NodeType::DELETE);
  auto *delete_ = reinterpret_cast<AST::DeleteExpression *>(node.get());
  ASSERT_FALSE(delete_->is_global);
  ASSERT_FALSE(delete_->is_array);

  ASSERT_EQ(delete_->expression->type, AST::NodeType::LITERAL);
  ASSERT_EQ(std::get<std::string>(dynamic_cast<AST::Literal *>(delete_->expression.get())->value.value), "x");
}

TEST(ParserTest, DeleteExpression_2) {
  ParserTester test{"::delete x;"};
  auto node = test->TryParseExpression(Precedence::kAll);
  ASSERT_EQ(test->current_token().type, TT_SEMICOLON);
  ASSERT_EQ(test.lexer.ReadToken().type, TT_ENDOFCODE);

  ASSERT_EQ(node->type, AST::NodeType::DELETE);
  auto *delete_ = reinterpret_cast<AST::DeleteExpression *>(node.get());
  ASSERT_TRUE(delete_->is_global);
  ASSERT_FALSE(delete_->is_array);

  ASSERT_EQ(delete_->expression->type, AST::NodeType::LITERAL);
  ASSERT_EQ(std::get<std::string>(dynamic_cast<AST::Literal *>(delete_->expression.get())->value.value), "x");
}

TEST(ParserTest, DeleteExpression_3) {
  ParserTester test{"delete[] x;"};
  auto node = test->TryParseExpression(Precedence::kAll);
  ASSERT_EQ(test->current_token().type, TT_SEMICOLON);
  ASSERT_EQ(test.lexer.ReadToken().type, TT_ENDOFCODE);

  ASSERT_EQ(node->type, AST::NodeType::DELETE);
  auto *delete_ = reinterpret_cast<AST::DeleteExpression *>(node.get());
  ASSERT_FALSE(delete_->is_global);
  ASSERT_TRUE(delete_->is_array);

  ASSERT_EQ(delete_->expression->type, AST::NodeType::LITERAL);
  ASSERT_EQ(std::get<std::string>(dynamic_cast<AST::Literal *>(delete_->expression.get())->value.value), "x");
}

TEST(ParserTest, DeleteExpression_4) {
  ParserTester test{"::delete[] x;"};
  auto node = test->TryParseExpression(Precedence::kAll);
  ASSERT_EQ(test->current_token().type, TT_SEMICOLON);
  ASSERT_EQ(test.lexer.ReadToken().type, TT_ENDOFCODE);

  ASSERT_EQ(node->type, AST::NodeType::DELETE);
  auto *delete_ = reinterpret_cast<AST::DeleteExpression *>(node.get());
  ASSERT_TRUE(delete_->is_global);
  ASSERT_TRUE(delete_->is_array);

  ASSERT_EQ(delete_->expression->type, AST::NodeType::LITERAL);
  ASSERT_EQ(std::get<std::string>(dynamic_cast<AST::Literal *>(delete_->expression.get())->value.value), "x");
}

TEST(ParserTest, SwitchStatement) {
  ParserTester test{"switch (5 * 6) { case 1: return 2 break case 2: return 3 break default: break };"};
  auto node = test->TryReadStatement();
  ASSERT_EQ(test->current_token().type, TT_SEMICOLON);
  ASSERT_EQ(test.lexer.ReadToken().type, TT_ENDOFCODE);

  ASSERT_EQ(node->type, AST::NodeType::SWITCH);
  auto *switch_ = dynamic_cast<AST::SwitchStatement *>(node.get());
  ASSERT_EQ(switch_->body->statements.size(), 3);

  ASSERT_EQ(switch_->body->statements[0]->type, AST::NodeType::CASE);
  auto *case1 = dynamic_cast<AST::CaseStatement *>(switch_->body->statements[0].get());
  ASSERT_EQ(case1->value->type, AST::NodeType::LITERAL);
  ASSERT_EQ(std::get<std::string>(dynamic_cast<AST::Literal *>(case1->value.get())->value.value), "1");
  ASSERT_EQ(case1->statements->statements.size(), 2);
  ASSERT_EQ(case1->statements->statements[0]->type, AST::NodeType::RETURN);
  ASSERT_EQ(case1->statements->statements[1]->type, AST::NodeType::BREAK);

  ASSERT_EQ(switch_->body->statements[1]->type, AST::NodeType::CASE);
  auto *case2 = dynamic_cast<AST::CaseStatement *>(switch_->body->statements[1].get());
  ASSERT_EQ(case2->value->type, AST::NodeType::LITERAL);
  ASSERT_EQ(std::get<std::string>(dynamic_cast<AST::Literal *>(case2->value.get())->value.value), "2");
  ASSERT_EQ(case2->statements->statements.size(), 2);
  ASSERT_EQ(case2->statements->statements[0]->type, AST::NodeType::RETURN);
  ASSERT_EQ(case2->statements->statements[1]->type, AST::NodeType::BREAK);

  ASSERT_EQ(switch_->body->statements[2]->type, AST::NodeType::DEFAULT);
  auto *default_ = dynamic_cast<AST::DefaultStatement *>(switch_->body->statements[2].get());
  ASSERT_EQ(default_->statements->statements.size(), 1);
  ASSERT_EQ(default_->statements->statements[0]->type, AST::NodeType::BREAK);
}

TEST(ParserTest, CodeBlock) {
  ParserTester test{"{ int x = 5 const int y = 6 float *(*z)[10] = nullptr foo(bar) }"};
  auto node = test->ParseCodeBlock();
  ASSERT_EQ(test->current_token().type, TT_ENDOFCODE);

  ASSERT_EQ(node->type, AST::NodeType::BLOCK);
  auto *block = dynamic_cast<AST::CodeBlock *>(node.get());
  ASSERT_EQ(block->statements.size(), 4);
  ASSERT_EQ(block->statements[0]->type, AST::NodeType::DECLARATION);
  ASSERT_EQ(block->statements[1]->type, AST::NodeType::DECLARATION);
  ASSERT_EQ(block->statements[2]->type, AST::NodeType::DECLARATION);
  ASSERT_EQ(block->statements[3]->type, AST::NodeType::FUNCTION_CALL);
}
