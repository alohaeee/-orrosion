#include "parser/parser.hpp"

namespace corrosion
{
	Pointer<Pat> Parser::parsePat()
	{
		return parsePatWithRangePat(true);
	}
	Pointer<Pat> Parser::parseTopPat(bool gateOr)
	{
		auto pat = parsePathWithOr(gateOr);
		return pat;
	}
	Pointer<Pat> Parser::parsePathWithOr(bool gateOr)
	{
		return nullptr;
	}
	Pointer<Pat> Parser::parsePatWithRangePat(bool allowRangePat)
	{
		auto lo = token.span;
		Pat::KindUnion kind;

		if (eat(TokenKind::BinOp, data::BinOp{ data::BinOp::And }) || eat(TokenKind::AndAnd))
		{
			kind = this->parsePatDeref();
		}
		else if (eat(TokenKind::OpenDelim, data::Delim{ data::Delim::Paren }))
		{
			kind = parsePatParen();
		}
		else if (eatKeyword(kw::Underscore))
		{
			kind = PatKind::Wild{};
		}
		else if (checkKeyword(kw::Mut) || checkKeyword(kw::Ref) || token.isIdent() && !token.isKeyword())
		{
			kind = parsePatIdentRef();
		}
		else
		{
			auto lit = parseLitMaybeMinus();
			if(lit)
			{
				kind = PatKind::Literal{lit};
			}
			else
			{
				m_session.errorSpan(lo.to(prevToken.span), "Expected pattern but found:");
				return nullptr;
			}

		}
		return MakePointer<Pat>(lo.to(prevToken.span),std::move(kind),DUMMY_NODE_ID);
	}
	PatKind::Ident Parser::parsePatIdent(BindingMode bindingMode)
	{
		auto ident = parseIdent();
		Pointer<Pat> sub;
		if (eat(TokenKind::At))
		{
			sub = parsePat();
		}
		return PatKind::Ident{bindingMode,ident,sub};
	}
	PatKind::Ident Parser::parsePatIdentRef()
	{
		BindingMode mode{mode.ByValue,Mutability::Not};
		if(eatKeyword(kw::Ref))
		{
			mode.kind = mode.ByRef;
		}
		if(eatKeyword(kw::Mut))
		{
			mode.mut = Mutability::Mut;
		}
		return parsePatIdent(mode);
	}


	/// Parse `&pat` / `&mut pat`.
	PatKind::Ref Parser::parsePatDeref()
	{
		auto mut = parseMutability();
		auto subpat = parsePatWithRangePat(false);
		return PatKind::Ref{ subpat, mut };
	}
	PatKind::Paren Parser::parsePatParen()
	{
		auto pat = parsePatWithRangePat(false);
		if(!eat(TokenKind::CloseDelim,data::Delim{data::Delim::Paren}))
		{
			m_session.errorSpan(pat->span, "paren pattern must have ')'");
		}
		return PatKind::Paren{ pat };
	}

}