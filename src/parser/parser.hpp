#ifndef CORROSION_SRC_PARSER_PARSER_HPP_
#define CORROSION_SRC_PARSER_PARSER_HPP_

#include "utility/std_incl.hpp"
#include "ast/ast.hpp"
#include "ast/assoc_prec.hpp"
#include "parse_session.hpp"
#include "token_stream.hpp"
#include "ast/token_tree.hpp"

using namespace corrosion::ast;
namespace corrosion
{
	struct TokenCursorFrame
	{
		ast::data::Delim delim;
		DelimSpan span;
		bool openDelim;
		TreeCursor cursor;
		bool closeDelim;

		TokenCursorFrame(data::Delim delim, DelimSpan&& span, TreeCursor stream) : openDelim{ delim.isEmpty()},
		closeDelim{delim.isEmpty()}, delim{delim}, span{span}, cursor{stream}
		{}
	};
	struct TokenCursor
	{
		Token next()
		{
			while(true)
			{
				TreeAndJoint tree{Token{},IsJoint::NotJoint};
				if(!this->frame.openDelim)
				{
					this->frame.openDelim = true;
					tree = {TokenTree::openTT(this->frame.span,this->frame.delim), IsJoint::NotJoint};
				}
				else if(auto next_tree = this->frame.cursor.nextWithJoint();next_tree)
				{
					tree = next_tree.value();
				}
				else if(!this->frame.closeDelim)
				{
					this->frame.closeDelim = true;
					tree = {TokenTree::closeTT(this->frame.span,this->frame.delim), IsJoint::NotJoint};
				}
				else if(stack.size())
				{
					this->frame = this->stack.back();
					this->stack.pop_back();
					continue;
				}
				else
				{
					return Token{TokenKind::Eof};
				}


				if(tree.first.isToken())
				{
					return tree.first.getToken();
				}
				else
				{
					auto delim = tree.first.getDelimited();
					this->stack.push_back(TokenCursorFrame{delim.kind,std::move(delim.span),std::move(delim.stream)});
				}
			}
		}
		std::vector<TokenCursorFrame> stack;
		TokenCursorFrame frame;
	};
	class Parser
	{
	 private:
//		inline std::optional<ast::TokenKind> look(std::size_t n=0) noexcept
//		{
//			return m_tokenStream.lookahead(n);
//		}
//		inline std::optional<ast::Token> token(std::size_t n=0) noexcept
//		{
//			return m_tokenStream.token();
//		}
//		inline std::optional<ast::Token> shift() noexcept
//		{
//			return m_tokenStream.shift();
//		}
		Token nextToken()
		{
			return tokenCursor.next();
		}
	 public:
		void shiftWith(Token&& nextTok)
		{
			if(this->prevToken.kind == TokenKind::Eof)
			{
				this->m_session.criticalSpan(nextTok.span,"attempted to bump the parser past EOF (may be stuck in a loop)");
			}
			this->prevToken = std::move(this->token);
			this->token = nextTok;

			this->expectedTokens.clear();
		}
		void shift()
		{
			auto next_tok = nextToken();
			shiftWith(std::move(next_tok));
		}

		/// Checks if the next token is `tok`, and returns `true` if so.
		///
		/// This method will automatically add `tok` to `expected_tokens` if `tok` is not
		/// encountered.
		bool check(TokenKind kind, TokenData&& data = data::Empty{})
		{
			auto is_present = this->token.kind == kind && this->token.data == data;
			if(!is_present)
			{
				this->expectedTokens.emplace_back(kind,data);
			}
			return is_present;
		}
		bool checkOrExpected(bool ok,TokenKind kind, const TokenData& data = data::Empty{})
		{
			if(ok)
			{
				return true;
			}
			expectedTokens.emplace_back(kind,data);
			return false;
		}
		bool checkPath()
		{
			return checkOrExpected(token.isPathStart(), token.kind,token.data);
		}
		bool checkKeyword(SymType sym)
		{
			return token.isKeyword(sym);
		}

		/// Consumes a token 'tok' if it exists. Returns whether the given token was present.
		bool eat(TokenKind kind, TokenData&& data = data::Empty{})
		{
			auto is_present = check(kind,std::move(data));
			if(is_present)
			{
				this->shift();
			}
			return is_present;

		}
		bool eatKeyword(SymType sym)
		{
			if(this->checkKeyword(sym))
			{
				this->shift();
				return true;
			}
			return false;
		}
		std::optional<Label> eatLabel()
		{
			if(auto ident = token.lifetime();ident)
			{
				this->shift();
				return Label{ident.value()};
			}
			return std::nullopt;
		}
		Token lookahead(std::size_t n)
		{
			auto tree = tokenCursor.frame.cursor.lookahead(n);
			if(tree)
			{
				if(tree->first.isToken())
				{
					return tree->first.getToken();
				}
				else
				{
					auto delim = tree->first.getDelimited();
					return Token{TokenKind::OpenDelim,delim.span.close,delim.kind};
				}

			}
			return Token{TokenKind::CloseDelim,tokenCursor.frame.span.close,tokenCursor.frame.delim};

		}
		/// Is the given keyword `kw` followed by a non-reserved identifier?
		bool isKwFollowedByIdent(SymType kw)
		{
			return token.isKeyword(kw) && !lookahead(1).isKeyword();
		}

		Mutability parseMutability()
		{
			if(eatKeyword(kw::Mut))
			{
				return Mutability::Mut;
			}
			return Mutability::Not;
		}
		Ident parseIdent()
		{
			if(token.isIdent())
			{
				auto ident = Ident{token.getData<data::Ident>().symbol,token.span};
				this->shift();
				return ident;
			}
			m_session.criticalSpan(token.span,"Trying to eat ident, but find: ");
		}

		void expect(TokenKind kind, TokenData&& data = data::Empty{})
		{
			if(expectedTokens.empty())
			{
				if(token.kind == kind && data == token.data)
				{
					this->shift();
				}
				m_session.criticalSpan(token.span,
					fmt::format("Expected token: {} , but found: ",Token{kind,{},data}.printable()));
			}
			else
			{
				std::string msg;
				for(auto &&[kind,data]: expectedTokens)
				{
					msg+=(fmt::format("({}) or ",Token{kind,{},data}.printable()));
				}
				msg.pop_back();
				msg.pop_back();
				msg.pop_back();
				m_session.criticalSpan(token.span,fmt::format("Expected: {}, \n but found:",msg));
			}
		}


//
//		Pointer<Expr> parseBlock();
//		Pointer<Expr> parseBlockLine();
//
//		Pointer<Expr> parseWhileExpr();
		Pointer<Expr> parseAssocExprWith(std::size_t minPrec, Pointer<Expr> lhs);
		Pointer<Expr> parsePrefixRangeExpr();
		Pointer<Expr> parsePrefixExpr();
		Pointer<Expr> parseDotOrCallExpr();
		Pointer<Expr> parseBottomExpr();
		Spanned<Pointer<Expr>> parsePrefixExprCommon(Span lo);
		Pointer<Expr> parseUnaryExpr(Span lo, UnOp op);
		Pointer<Expr> parseLitExpr();
		Pointer<Expr> parseLitMaybeMinus();
		Pointer<Expr> parseCondExpr();
		Pointer<Expr> parseWhileExpr();

		Pointer<Expr> parseForExpr();

		Pointer<Block> parseBlockCommon();

		Pointer<Stmt> parseStmtWithoutRecovery();
		Pointer<Local> parseLocal();



		inline Pointer<Pat> parsePat();
		Pointer<Pat> parseTopPat(bool gateOr);
		Pointer<Pat> parsePathWithOr(bool gateOr);
		Pointer<Pat> parsePatWithRangePat(bool allowRangePat);
		PatKind::Ref parsePatDeref();
		PatKind::Paren parsePatParen();
		PatKind::Ident parsePatIdentRef();
		PatKind::Ident parsePatIdent(BindingMode bindingMode);

		//diagnistics

		std::optional<Pointer<Expr>> checkNoChainedComparison(Pointer<Expr> innerOp,Spanned<AssocOp> outerOp);


		TokenStreamV m_tokenStream{};
		ParseSession m_session;
		TokenStream tokens;


		/// The current token.
		Token token;
		/// The previous token.
		Token prevToken;
		std::vector<std::pair<TokenKind,TokenData>> expectedTokens;
		TokenCursor tokenCursor;

	 	//Item root;

	};
}

#endif //CORROSION_SRC_PARSER_PARSER_HPP_