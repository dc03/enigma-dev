#include <string>
using std::string;


#include "parser/lex_edl.h"
#include "parser/lex_edl.h"
#include "languages/language_adapter.h"
#include "Parser/bodies.h"
#include "System/builtins.h"

using namespace jdip;


/// Structure representing an event, such as create, destroy, key press, collision, etc.
struct egm_event {
  int main_id; ///< The ID of the category of this event, such as a keyboard or mouse event
  int id; ///< The secondary ID of this event, where applicable.
  /// Comparator to allow use of this class as a key
  inline bool operator< (const egm_event &other) const {
    if (main_id > other.main_id) return false;
    return main_id < other.main_id || id < other.id;
  }
};

enum ScopeFlag {
  LOCAL,
  GLOBAL,
  GLOBALLOCAL
};

namespace settings {
  extern bool pedantic_edl;
  extern bool pedantic_errors;
}

bool pedantic_warn(token_t &token, error_handler *herr, string w);

struct declaration {
  definition *def;
  AST *initialization;
  inline declaration(definition *d = NULL, AST* init = NULL): def(d), initialization(init) {}
};

struct EDL_AST: AST {
  /// General parent
  struct AST_Node_Statement: AST::AST_Node {
    virtual string toString() const; ///< Renders this node and its children as a string, recursively.
    virtual string toString(int indent) const = 0; ///< Renders this node and its children as a string, recursively.
    
    AST_Node_Statement();
    virtual ~AST_Node_Statement();
  };
  
  struct AST_Node_Statement_Standard: AST_Node_Statement {
    AST_Node *statement; ///< The actual content of this statement; a nested AST
    string toString(int indent) const;
  };
  
  /// AST node representing a block of code, which is just a statement composed of other statements.
  struct AST_Node_Block: AST_Node_Statement {
    vector<AST_Node_Statement*> statements; ///< Statements and operations in the order they are to be executed.
    
    AST_Node_Block(); ///< Default constructor.
    ~AST_Node_Block(); ///< Default destructor. Frees children recursively.
    
    virtual string toString(int indent) const; ///< Renders this node and its children as a string, recursively.
    virtual void toSVG(int x, int y, SVGrenderInfo* svg); ///< Renders this node and its children as an SVG.
    virtual int width(); ///< Returns the width which will be used to render this node and all its children.
    virtual int height(); ///< Returns the height which will be used to render this node and all its children.
  };
  
  /// AST node representing a set of declarations
  struct AST_Node_Declaration: AST_Node_Statement {
    full_type base_type; ///< The type used to begin this declaration.
    vector<declaration> declarations; ///< Array of objects declared here
    ScopeFlag scope; ///< The scope in which this was declared.
    string toString(int lvl) const; ///< Convert back to a string
    AST_Node_Declaration(const full_type &ft);
  };
  
  struct AST_Node_Structdef: AST_Node_Statement {
    vector<AST_Node_Declaration*> members;
    definition *def;
    
    string toString(int) const;
  };
  
  struct AST_Node_Enumdef: AST_Node_Statement {
    vector<AST_Node_Declaration*> members;
    definition *def;
    
    string toString(int) const;
  };
  
  /// AST Node specifically representing an if statement.
  struct AST_Node_Statement_if: AST_Node_Statement {
    AST_Node *condition; ///< The condition to check
    AST_Node_Statement *do_if; ///< The block of code to execute if the condition is met
    AST_Node_Statement *do_else; ///< The block of code to execute if the condition is not met
    
    virtual string toString(int indent) const; ///< Renders this node and its children as a string, recursively.
    virtual void toSVG(int x, int y, SVGrenderInfo* svg); ///< Renders this node and its children as an SVG.
    virtual int width(); ///< Returns the width which will be used to render this node and all its children.
    virtual int height(); ///< Returns the height which will be used to render this node and all its children.
    
    AST_Node_Statement_if(AST_Node *cd = NULL, AST_Node_Statement *doif = NULL, AST_Node_Statement *doelse = NULL); ///< Default constructor
    ~AST_Node_Statement_if();
  };
  
  /// AST Node specifically representing a for statement.
  struct AST_Node_Statement_for: AST_Node_Statement {
    AST_Node_Statement *operand_pre;
    AST_Node *condition;
    AST_Node_Statement *operand_post;
    AST_Node_Statement *code;
    
    string toString(int indent) const; ///< Renders this node and its children as a string, recursively.
    void toSVG(int x, int y, SVGrenderInfo* svg); ///< Renders this node and its children as an SVG.
    int width(); ///< Returns the width which will be used to render this node and all its children.
    int height(); ///< Returns the height which will be used to render this node and all its children.
    
    AST_Node_Statement_for(AST_Node_Statement *opre = NULL, AST_Node *cond = NULL, AST_Node_Statement *opost = NULL, AST_Node_Statement *loop = NULL); ///< Default constructor
    ~AST_Node_Statement_for();
  };
  
  /// AST Node specifically representing a repeat statement.
  struct AST_Node_Statement_repeat: AST_Node_Statement {
    AST_Node *condition; ///< The condition upon which this loop repeats (or halts, depending on #negate).
    AST_Node_Statement *code; ///< The code to perform during the loop
    
    string toString(int indent) const; ///< Renders this node and its children as a string, recursively.
    void toSVG(int x, int y, SVGrenderInfo* svg); ///< Renders this node and its children as an SVG.
    int width(); ///< Returns the width which will be used to render this node and all its children.
    int height(); ///< Returns the height which will be used to render this node and all its children.
    
    AST_Node_Statement_repeat(AST_Node *cond = NULL, AST_Node_Statement *code = NULL);
    ~AST_Node_Statement_repeat();
  };
  
  /// AST Node specifically representing a while or until statement.
  struct AST_Node_Statement_while: AST_Node_Statement {
    AST_Node *condition; ///< The condition upon which this loop repeats (or halts, depending on #negate).
    AST_Node_Statement *code; ///< The code to perform during the loop
    bool negate; ///< True if this is actually an `until' statement
        
    string toString(int indent) const; ///< Renders this node and its children as a string, recursively.
    void toSVG(int x, int y, SVGrenderInfo* svg); ///< Renders this node and its children as an SVG.
    int width(); ///< Returns the width which will be used to render this node and all its children.
    int height(); ///< Returns the height which will be used to render this node and all its children.
    
    AST_Node_Statement_while(AST_Node *cond = NULL, AST_Node_Statement *code = NULL, bool negate = false);
    ~AST_Node_Statement_while();
  };
  
  /// AST Node specifically representing a with statement.
  struct AST_Node_Statement_with: AST_Node_Statement {
    AST_Node *instances; ///< The instances which will be iterated.
    AST_Node_Statement *code; ///< The code to perform during the loop
    
    virtual string toString(int indent) const; ///< Renders this node and its children as a string, recursively.
    virtual void toSVG(int x, int y, SVGrenderInfo* svg); ///< Renders this node and its children as an SVG.
    virtual int width(); ///< Returns the width which will be used to render this node and all its children.
    virtual int height(); ///< Returns the height which will be used to render this node and all its children.
    
    AST_Node_Statement_with(AST_Node *whom = NULL, AST_Node_Statement* code = NULL);
    ~AST_Node_Statement_with();
  };
  
  struct AST_Node_Statement_case;
  struct AST_Node_Statement_default;
  
  /// AST Node specifically representing a with statement.
  struct AST_Node_Statement_switch: AST_Node_Statement {
    AST_Node *expression; ///< The expression to switch.
    AST_Node_Statement *code; ///< The code to perform during the loop
    vector<AST_Node_Statement*> sw_cases; ///< Cases in this switch block
    AST_Node_Statement_default* sw_default; ///< The default jump target
    
    void add_case(AST_Node_Statement_case* sc); ///< Adds a case label to this switch statement, updating the given Statement_case to reflect its containing construct.
    int  add_default(AST_Node_Statement_default* sd, error_handler* herr); ///< Sets the default label's statement, returning 0 for success or 1 if an error is thrown.
    
    virtual string toString(int indent) const; ///< Renders this node and its children as a string, recursively.
    virtual void toSVG(int x, int y, SVGrenderInfo* svg); ///< Renders this node and its children as an SVG.
    virtual int width(); ///< Returns the width which will be used to render this node and all its children.
    virtual int height(); ///< Returns the height which will be used to render this node and all its children.
    
    AST_Node_Statement_switch(AST_Node *swexp = NULL, AST_Node_Statement *swcode = NULL); ///< Construct with an expression and code block, or construct default.
    ~AST_Node_Statement_switch();
  };
  
  /// AST Node specifically representing a try-catch statement.
  struct AST_Node_Statement_trycatch: AST_Node_Statement {
    AST_Node_Statement *code_try; ///< The code to try
    
    struct catch_clause {
      full_type type_catch; ///< The type to be caught
      AST_Node_Statement *code_catch; ///< The code to perform if an exception is caught
      inline catch_clause(): code_catch(0) {}
      inline catch_clause(full_type tc, AST_Node_Statement *cc): type_catch(tc), code_catch(cc) {}
      inline ~catch_clause() { delete code_catch; }
    };
    vector<catch_clause> catches; ///< Catch clauses
      
    virtual string toString(int indent) const; ///< Renders this node and its children as a string, recursively.
    virtual void toSVG(int x, int y, SVGrenderInfo* svg); ///< Renders this node and its children as an SVG.
    virtual int width(); ///< Returns the width which will be used to render this node and all its children.
    virtual int height(); ///< Returns the height which will be used to render this node and all its children.
    
    AST_Node_Statement_trycatch(AST_Node_Statement *code_try = NULL);
    ~AST_Node_Statement_trycatch();
  };
  
  /// AST Node specifically representing a do statement.
  struct AST_Node_Statement_do: AST_Node_Statement {
    AST_Node_Statement *code; ///< The code to perform during the loop.
    AST_Node *condition; ///< The condition to check.
    bool negate; ///< True if this is a do-until loop.
    
    virtual string toString(int indent) const; ///< Renders this node and its children as a string, recursively.
    virtual void toSVG(int x, int y, SVGrenderInfo* svg); ///< Renders this node and its children as an SVG.
    virtual int width(); ///< Returns the width which will be used to render this node and all its children.
    virtual int height(); ///< Returns the height which will be used to render this node and all its children.
    
    AST_Node_Statement_do(AST_Node_Statement *loop = NULL, AST_Node *cond = NULL, bool negate = false);
    ~AST_Node_Statement_do();
  };
  
  /// AST Node specifically representing a return statement.
  struct AST_Node_Statement_return: AST_Node_Statement {
    AST_Node *value; ///< The expression given to this statement, eg, the "a + 2" in "return a + 2;".
    
    string toString(int indent) const; ///< Renders this node and its children as a string, recursively.
    void toSVG(int x, int y, SVGrenderInfo* svg); ///< Renders this node and its children as an SVG.
    int width(); ///< Returns the width which will be used to render this node and all its children.
    int height(); ///< Returns the height which will be used to render this node and all its children.
  };
  
  /// AST Node specifically representing a default label.
  struct AST_Node_Statement_default: AST_Node_Statement {
    AST_Node_Statement_switch *st_switch;
    
    virtual string toString(int indent) const; ///< Renders this node and its children as a string, recursively.
    virtual void toSVG(int x, int y, SVGrenderInfo* svg); ///< Renders this node and its children as an SVG.
    virtual int width(); ///< Returns the width which will be used to render this node and all its children.
    virtual int height(); ///< Returns the height which will be used to render this node and all its children.
    
    AST_Node_Statement_default(AST_Node_Statement_switch *st_switch);
  };
  
  /// AST Node specifically representing a case label.
  struct AST_Node_Statement_case: AST_Node_Statement_default {
    AST_Node* value;
    
    string toString(int indent) const; ///< Renders this node and its children as a string, recursively.
    void toSVG(int x, int y, SVGrenderInfo* svg); ///< Renders this node and its children as an SVG.
    int width(); ///< Returns the width which will be used to render this node and all its children.
    int height(); ///< Returns the height which will be used to render this node and all its children.
    
    AST_Node_Statement_case(AST_Node_Statement_switch *st_switch, AST_Node *val = NULL);
    ~AST_Node_Statement_case();
  };
  
  /// Enumeration of kinds of statements
  enum statement_kind {
    SK_CONDITIONAL = 0x0001, ///< A raw conditional, namely `if'
    SK_LOOP        = 0x0002, ///< Any sort of loop, eg `repeat', `while', `until', `do-while', `with'
    SK_SWITCH      = 0x0004, ///< A `switch' statement
    SK_WITH        = 0x000A, ///< A `with' statement
    SK_TRYCATCH    = 0x0010, ///< A `try-catch' statement
    SK_BREAKABLE   = 0x000E, ///< Any statement inside which a `break' or `continue' may appear; check (sk & SK_BREAKABLE) != 0
    SK_CONTINUABLE = 0x000A  ///< Any statement inside which a `break' or `continue' may appear; check (sk & SK_BREAKABLE) != 0
  };
  /// Structure representing the location of a statement in a piece of code.
  struct statement_ref {
    statement_kind kind;
    AST_Node_Statement* statement; ///< The statement node in memory
    int line; ///< The line on which the statement appears in the code
    int pos; ///< The position in the line at which the statement appears
  };
  /// Stack of statement_refs
  typedef vector<statement_ref> loopstack;
  /// The stack of statement_refs indicating what loops we are inside
  loopstack loops;
  /// Fetch a loop from our stack which can be break'd, or NULL.
  statement_ref *loops_get_breakable(statement_kind mask = SK_BREAKABLE);
  /// Fetch a loop from our stack of a certain type (must match @p kind exactly), returning NULL on no match.
  statement_ref *loops_get_kind(statement_kind kind);
  
  /// The scope of the object which will receive this AST
  definition_scope *object_scope;
  
  /// The scope of the object which will receive this AST
  definition_scope *global_scope;

  /**
    Handle parsing a structure or union, per EDL specification. Upon invocation, the given
    token is expected to denote whether the object parsed is a structure or union (ie, it
    should be either TT_STRUCT or TT_UNION).
    @param lex    The lexer to poll for tokens.
    @param token  The first token in this scope.
    @param scope  The scope to search for definitions.
    @param object_scope  The scope into which objects declared "local" will be placed.
                         This can be the same as @p scope, though generally it should not be.
    @param global_scope  The scope into which objects declared "global" will be placed.
                         This should almost certainly not be the same as @p scope.
    @param loops  A stack of breakable loops the current code is nested in.
    @param herr   An error handler to which EDL syntax errors will be reported.
    @return  Returns the structure definition as an AST node.
  */
  AST_Node_Structdef   *handle_struct     (token_t &token);
  AST_Node_Enumdef     *handle_enum       (token_t &token);
  AST_Node_Declaration *handle_declaration(token_t &token);

  /**
    The main EDL parse call. To parse a piece of code, establish a lexer for it and
    invoke this function with @p token as the first token in the file and @p scope as
    the containing event, or just as a scope where there is no event associated. Upon
    completion of this function, @p token should be set to TT_ENDOFCODE. In the event
    that it is not, you should handle the error accordingly. Internally, this function
    will be recursively invoked with the expectation of ending on a closing brace token.
    @param lex    The lexer to poll for tokens.
    @param token  The first token in this scope.
    @param scope  The scope to search for definitions.
    @param object_scope  The scope into which objects declared "local" will be placed.
                         This can be the same as @p scope, though generally it should not be.
    @param global_scope  The scope into which objects declared "global" will be placed.
                         This should almost certainly not be the same as @p scope.
    @param loops  A stack of breakable loops the current code is nested in.
    @param herr   An error handler to which EDL syntax errors will be reported.
    @return  Returns the new statement as an AST node.
  */
  AST_Node_Statement*          handle_statement(token_t &token);
  AST_Node_Block*              handle_block    (token_t &token);
  AST_Node_Statement_repeat*   handle_repeat   (token_t &token);
  AST_Node_Statement_return*   handle_return   (token_t &token);
  AST_Node_Statement_if*       handle_if       (token_t &token);
  AST_Node_Statement_for*      handle_for      (token_t &token);
  AST_Node_Statement_switch*   handle_switch   (token_t &token);
  AST_Node_Statement_do*       handle_do       (token_t &token);
  AST_Node_Statement_while*    handle_while    (token_t &token);
  AST_Node_Statement_with*     handle_with     (token_t &token);
  AST_Node_Statement_trycatch* handle_trycatch (token_t &token);
  
  bool parse_edl(string code);
};