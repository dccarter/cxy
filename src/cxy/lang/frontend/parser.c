/**
 * Credits: https://github.com/madmann91/fu/blob/master/src/fu/lang/parser.c
 */

#include "parser.h"
#include "ast.h"
#include "defines.h"
#include "flag.h"
#include "lexer.h"
#include "strings.h"

#include "driver/cc.h"
#include "driver/driver.h"
#include "driver/plugin.h"

#include "core/alloc.h"
#include "core/e4c.h"
#include "core/mempool.h"

#include "../operations.h"
#include "lang/middle/builtins.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

E4C_DEFINE_EXCEPTION(ParserException, "Parsing error", RuntimeException);
E4C_DEFINE_EXCEPTION(ParserAbort, "Failed error", RuntimeException);
E4C_DEFINE_EXCEPTION(ErrorLimitExceeded,
                     "Error limit exceeded",
                     RuntimeException);

static void synchronize(Parser *P);

static void synchronizeUntil(Parser *P, TokenTag tag);

static AstNode *expression(Parser *P, bool allowStructs);

static AstNode *statement(Parser *P, bool exprOnly);

static inline AstNode *statementOrExpression(Parser *P)
{
    return statement(P, true);
}

static inline AstNode *statementOnly(Parser *P) { return statement(P, false); }

static AstNode *parseType(Parser *P);

static AstNode *parseUnionType(Parser *P);

static AstNode *primary(Parser *P, bool allowStructs);
static AstNode *postfixCast(Parser *P, AstNode *expr, bool allowStructs);
static AstNode *macroExpression(Parser *P, AstNode *callee);
static AstNode *launchExpression(Parser *P);

static AstNode *callExpression(Parser *P, AstNode *callee);

static AstNode *parsePath(Parser *P);

static AstNode *variable(
    Parser *P, bool isPublic, bool isExport, bool isExpression, bool woInit);

static AstNode *funcDecl(Parser *P, u64 flags);

static AstNode *aliasDecl(Parser *P, bool isPublic, bool isExtern);

static AstNode *enumDecl(Parser *P, bool isPublic);

static AstNode *classOrStructDecl(Parser *P, bool isPublic, bool isExtern);

static AstNode *attributes(Parser *P);

static AstNode *substitute(Parser *P, bool allowStructs);

static AstNode *block(Parser *P);

static AstNode *comptime(Parser *P, AstNode *(*parser)(Parser *));

static AstNode *comptimeDeclaration(Parser *P);

static void listAddAstNode(AstNodeList *list, AstNode *node)
{
    if (!list->last)
        list->first = node;
    else
        list->last->next = node;
    list->last = getLastAstNode(node);
}

static inline const char *getTokenString(Parser *P, const Token *tok, bool trim)
{
    size_t start = tok->fileLoc.begin.byteOffset + trim;
    size_t size = tok->fileLoc.end.byteOffset - trim - start;
    if (tok->buffer->fileData[start] == '`' &&
        tok->buffer->fileData[start + size - 1] == '`') {
        return makeStringSized(
            P->strPool, &tok->buffer->fileData[start + 1], size - 2);
    }
    return makeStringSized(P->strPool, &tok->buffer->fileData[start], size);
}

static inline const char *getStringLiteral(Parser *P, const Token *tok)
{
    size_t start = tok->fileLoc.begin.byteOffset + 1;
    size_t size = tok->fileLoc.end.byteOffset - start - 1;
    char *str = mallocOrDie(size + 1), *p = str;
    size = escapeString(&tok->buffer->fileData[start], size, str, size);

    const char *s = makeStringSized(P->strPool, str, size);
    free(str);
    return s;
}

static inline Token *current(Parser *parser) { return &parser->ahead[1]; }

static inline Token *previous(Parser *parser) { return &parser->ahead[0]; }

static Token advanceLexer_(Parser *P);

static inline Token *advance(Parser *parser)
{
    if (current(parser)->tag != tokEoF) {
        parser->ahead[0] = parser->ahead[1];
        parser->ahead[1] = parser->ahead[2];
        parser->ahead[2] = parser->ahead[3];
        parser->ahead[3] = advanceLexer_(parser);
    }

    return previous(parser);
}

static inline Token *peek(Parser *parser, u32 index)
{
    csAssert(index <= 2, "len out of bounds");
    return &parser->ahead[1 + index];
}

static Token *parserCheck(Parser *parser, const TokenTag tags[], u32 count)
{
    for (u32 i = 0; i < count; i++) {
        if (current(parser)->tag == tags[i])
            return current(parser);
    }
    return NULL;
}

static Token *parserCheckPeek(Parser *parser,
                              u32 index,
                              const TokenTag tags[],
                              u32 count)
{
    Token *tok = peek(parser, index);
    if (tok == NULL)
        return tok;

    for (u32 i = 0; i < count; i++) {
        if (tok->tag == tags[i])
            return tok;
    }
    return NULL;
}

static Token *parserMatch(Parser *parser, TokenTag tags[], u32 count)
{
    if (parserCheck(parser, tags, count))
        return advance(parser);
    return NULL;
}

// clang-format off
#define check(P, ...) \
({ TokenTag LINE_VAR(tags)[] = { __VA_ARGS__, tokEoF }; parserCheck((P), LINE_VAR(tags), sizeof__(LINE_VAR(tags))-1); })

#define checkPeek(P, I, ...) \
({ TokenTag LINE_VAR(tags)[] = { __VA_ARGS__, tokEoF }; parserCheckPeek((P), (I), LINE_VAR(tags), sizeof__(LINE_VAR(tags))-1); })

#define match(P, ...) \
({ TokenTag LINE_VAR(mtags)[] = { __VA_ARGS__, tokEoF }; parserMatch((P), LINE_VAR(mtags), sizeof__(LINE_VAR(mtags))-1); })

// clang-format on

static bool isEoF(Parser *parser) { return current(parser)->tag == tokEoF; }

static void parserError(Parser *parser,
                        const FileLoc *loc,
                        cstring msg,
                        FormatArg *args)
{
    FileLoc copy = *loc;
    advance(parser);
    logError(parser->L, &copy, msg, args);
    E4C_THROW_CTX(ParserException, "", parser);
}

static void parserFail(Parser *parser,
                       const FileLoc *loc,
                       cstring msg,
                       FormatArg *args)
{
    FileLoc copy = *loc;
    advance(parser);
    logError(parser->L, &copy, msg, args);
    E4C_THROW_CTX(ParserAbort, "", parser);
}

static void parserWarnThrow(Parser *parser,
                            const FileLoc *loc,
                            cstring msg,
                            FormatArg *args)
{
    advance(parser);
    logWarning(parser->L, loc, msg, args);
    E4C_THROW_CTX(ParserException, "", parser);
}

static inline bool isEndOfStmt(Parser *P)
{
    switch (current(P)->tag) {
    case tokEoF:
    case tokSemicolon:
    case tokAsync:
    case tokVar:
    case tokWhile:
    case tokFor:
    case tokLBrace:
    case tokRBrace:
    case tokConst:
    case tokIf:
    case tokElse:
    case tokSwitch:
    case tokMatch:
    case tokBreak:
    case tokContinue:
    case tokDefer:
    case tokReturn:
    case tokDelete:
    case tokRaise:
        return true;
    case tokHash:
        if (checkPeek(P, 1, tokIf, tokFor, tokVar))
            return true;
    default:
        return false;
    }
}

static inline Token advanceLexer_(Parser *P)
{
    Token tok = advanceLexer(P->lexer);
    if (tok.tag != tokInclude)
        return tok;

    tok = advanceLexer(P->lexer);
    if (tok.tag != tokStringLiteral) {
        parserFail(P,
                   &tok.fileLoc,
                   "include must be followed the file path to include",
                   NULL);
    }

    cstring filename = getStringLiteral(P, &tok);
    cstring path = getIncludeFileLocation(P->cc, &tok.fileLoc, filename);
    if (path == NULL) {
        E4C_THROW_CTX(ParserAbort, "", P);
        unreachable();
    }

    if (access(path, F_OK) != 0) {
        parserFail(P,
                   &tok.fileLoc,
                   "include file '{s}' resolved to '{s}' does not exist",
                   (FormatArg[]){{.s = filename}, {.s = path}});
    }
    lexerPush(P->lexer, path);
    return advanceLexer_(P);
}

static Token *consume(Parser *parser, TokenTag id, cstring msg, FormatArg *args)
{
    Token *tok = check(parser, id);
    if (tok == NULL) {
        const Token curr = *current(parser);
        parserError(parser, &curr.fileLoc, msg, args);
    }

    return advance(parser);
}

static Token *consume0(Parser *parser, TokenTag id)
{
    return consume(
        parser,
        id,
        "unexpected token, expecting '{s}', but got '{s}'",
        (FormatArg[]){{.s = token_tag_to_str(id)},
                      {.s = token_tag_to_str(current(parser)->tag)}});
}

static void reportUnexpectedToken(Parser *P, cstring expected)
{
    Token cur = *current(P);
    parserError(
        P,
        &cur.fileLoc,
        "unexpected token '{s}', expecting {s}",
        (FormatArg[]){{.s = token_tag_to_str(cur.tag)}, {.s = expected}});
}

AstNode *newAstNode(Parser *P, const Token *start, const AstNode *init)
{
    AstNode *node = allocFromMemPool(P->memPool, sizeof(AstNode));
    memcpy(node, init, sizeof(AstNode));
    node->loc.fileName = start->buffer->fileName;
    node->loc.begin = start->fileLoc.begin;
    if (start->buffer->fileName == previous(P)->fileLoc.fileName)
        node->loc.end = previous(P)->fileLoc.end;
    else
        node->loc.end = start->buffer->filePos;
    return node;
}

AstNode *newAstNode_(Parser *P, const FileLoc *start, const AstNode *init)
{
    AstNode *node = allocFromMemPool(P->memPool, sizeof(AstNode));
    memcpy(node, init, sizeof(AstNode));
    node->loc.fileName = start->fileName;
    node->loc.begin = start->begin;
    if (start->fileName == previous(P)->buffer->fileName)
        node->loc.end = previous(P)->fileLoc.end;
    else
        node->loc.end = start->end;
    return node;
}

static AstNode *parseMany(Parser *P,
                          TokenTag stop,
                          TokenTag sep,
                          AstNode *(with)(Parser *))
{
    AstNodeList list = {NULL};
    while (!check(P, stop) && !isEoF(P)) {
        listAddAstNode(&list, with(P));
        if (!match(P, sep) && !check(P, stop)) {
            parserError(P,
                        &current(P)->fileLoc,
                        "unexpected token '{s}', expecting '{s}' or '{s}'",
                        (FormatArg[]){{.s = token_tag_to_str(current(P)->tag)},
                                      {.s = token_tag_to_str(stop)},
                                      {.s = token_tag_to_str(sep)}});
        }
    }

    return list.first;
}

static AstNode *parseMany2(Parser *P,
                           TokenTag stop1,
                           TokenTag stop2,
                           TokenTag sep,
                           AstNode *(with)(Parser *))
{
    AstNodeList list = {NULL};
    while (!check(P, stop1, stop2) && !isEoF(P)) {
        listAddAstNode(&list, with(P));
        if (!match(P, sep) && !check(P, stop1, stop2)) {
            parserError(P,
                        &current(P)->fileLoc,
                        "unexpected token '{s}', expecting '{s}/{s}' or '{s}'",
                        (FormatArg[]){{.s = token_tag_to_str(current(P)->tag)},
                                      {.s = token_tag_to_str(stop1)},
                                      {.s = token_tag_to_str(stop2)},
                                      {.s = token_tag_to_str(sep)}});
        }
    }

    return list.first;
}

static inline AstNode *parseManyNoSeparator(Parser *P,
                                            TokenTag stop,
                                            AstNode *(with)(Parser *))
{
    AstNodeList list = {NULL};
    while (!check(P, stop) && !isEoF(P)) {
        AstNode *node = with(P);
        if (node)
            listAddAstNode(&list, node);
    }

    return list.first;
}

static AstNode *parseAtLeastOne(Parser *P,
                                cstring msg,
                                TokenTag stop,
                                TokenTag start,
                                AstNode *(with)(Parser *P))
{
    AstNode *nodes = parseMany(P, stop, start, with);
    if (nodes == NULL) {
        parserError(P,
                    &current(P)->fileLoc,
                    "expecting at least 1 {s}",
                    (FormatArg[]){{.s = msg}});
    }

    return nodes;
}

static u64 skipEnclosedCode(Parser *P, TokenTag open, TokenTag close)
{

    if (!match(P, open))
        return 0;

    u64 count = 1;
    while (!check(P, tokEoF) && count > 0) {
        TokenTag tok = current(P)->tag;
        if (tok == open)
            count++;
        else if (tok == close)
            count--;
        advance(P);
    }

    return count;
}

static bool skipUntil(Parser *P, TokenTag tok)
{

    while (!check(P, tok, tokEoF)) {
        advance(P);
    }

    return check(P, tok);
}

static inline AstNode *parseNull(Parser *P)
{
    const Token *tok = consume0(P, tokNull);
    return newAstNode(P, tok, &(AstNode){.tag = astNullLit});
}

static inline AstNode *parseBool(Parser *P)
{
    const Token *tok = match(P, tokTrue, tokFalse);
    if (tok == NULL) {
        reportUnexpectedToken(P, "bool literals i.e 'true'/'false'");
    }
    return newAstNode(P,
                      tok,
                      &(AstNode){.tag = astBoolLit,
                                 .boolLiteral.value = tok->tag == tokTrue});
}

static inline AstNode *parseChar(Parser *P)
{
    const Token *tok = consume0(P, tokCharLiteral);
    AstNode *node = newAstNode(
        P, tok, &(AstNode){.tag = astCharLit, .charLiteral.value = tok->cVal});

    if (match(P, tokQuote)) {
        AstNode *type = parseType(P);
        return newAstNode(
            P,
            tok,
            &(AstNode){.tag = astTypedExpr,
                       .typedExpr = {.expr = node, .type = type}});
    }

    return node;
}

static inline AstNode *parseInteger(Parser *P, bool isNegative)
{
    const Token prev = *previous(P);
    const Token tok = *consume0(P, tokIntLiteral);
    AstNode *type = NULL;
    AstNode *node = newAstNode(
        P,
        (isNegative ? &prev : &tok),
        &(AstNode){.tag = astIntegerLit, .intLiteral.uValue = tok.iVal});

    if (isNegative) {
        node->intLiteral.isNegative = true;
        node->intLiteral.value = -tok.iVal;
    }

    if (match(P, tokQuote)) {
        type = parseType(P);
        return newAstNode(
            P,
            (isNegative ? &tok : &prev),
            &(AstNode){.tag = astTypedExpr,
                       .typedExpr = {.expr = node, .type = type}});
    }
    return node;
}

static inline AstNode *parseFloat(Parser *P, bool isNegative)
{
    const Token prev = *previous(P);
    const Token *tok = consume0(P, tokFloatLiteral);
    AstNode *node = newAstNode(
        P,
        tok,
        &(AstNode){.tag = astFloatLit,
                   .floatLiteral.value = isNegative ? -tok->fVal : tok->fVal});
    if (match(P, tokQuote)) {
        AstNode *type = parseType(P);
        return newAstNode(
            P,
            (isNegative ? &prev : tok),
            &(AstNode){.tag = astTypedExpr,
                       .typedExpr = {.expr = node, .type = type}});
    }
    return node;
}

static inline AstNode *parseString(Parser *P)
{
    const Token tok = *consume0(P, tokStringLiteral);
    AstNode *node = newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astStringLit,
                   .stringLiteral.value = getStringLiteral(P, &tok)});

    if (match(P, tokDot)) {
        Token kind = *consume0(P, tokIdent);
        size_t start = kind.fileLoc.begin.byteOffset;
        size_t len = kind.fileLoc.end.byteOffset - start;
        const char c = kind.buffer->fileData[start];

        cstring type = NULL;
        if (c == 's' && len == 1) {
            type = S___string;
        }
        else if (c == 'S' && len == 1) {
            type = S_String;
        }
        else {
            parserError(
                P,
                &kind.fileLoc,
                "unexpected string literal suffix, expecting and `s` or `S`",
                NULL);
            unreachable();
        }
        node = newAstNode(
            P,
            &tok,
            &(AstNode){
                .tag = astCallExpr,
                .callExpr = {.callee = makePath(
                                 P->memPool, &tok.fileLoc, type, flgNone, NULL),
                             node}});
    }
    return node;
}

static inline AstNode *parseIdentifier(Parser *P)
{
    const Token tok = *consume0(P, tokIdent);
    AstNode *ident =
        newAstNode(P,
                   &tok,
                   &(AstNode){.tag = astIdentifier,
                              .ident.value = getTokenString(P, &tok, false)});
    if (check(P, tokLNot) && !checkPeek(P, 1, tokAs))
        return macroExpression(P, ident);

    return ident;
}

static inline AstNode *parseSymbol(Parser *P)
{
    const Token tok = *consume0(P, tokColon);
    const Token name = *consume0(P, tokIdent);
    AstNode *ident = newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astStringLit,
                   .stringLiteral.value = getTokenString(P, &name, false)});
    return ident;
}

static inline AstNode *parseIdentifierWithAlias(Parser *P)
{
    Token tok = *consume0(P, tokIdent);
    cstring name = getTokenString(P, &tok, false);

    cstring alias = NULL;
    if (match(P, tokFatArrow)) {
        alias = name;
        name = getTokenString(P, consume0(P, tokIdent), false);
    }

    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = astIdentifier,
                                 .ident = {.value = name, .alias = alias}});
}

static inline AstNode *parseMacroParam(Parser *P)
{
    bool isVariadic = match(P, tokElipsis);
    const Token tok = *consume0(P, tokIdent);
    AstNode *ident =
        newAstNode(P,
                   &tok,
                   &(AstNode){.tag = astIdentifier,
                              .flags = isVariadic ? flgVariadic : flgNone,
                              .ident.value = getTokenString(P, &tok, false)});

    return ident;
}

static inline AstNode *primitive(Parser *P)
{
    const Token tok = *current(P);
    if (!isPrimitiveType(tok.tag)) {
        reportUnexpectedToken(P, "a primitive type");
    }
    advance(P);
    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astPrimitiveType,
                   .primitiveType.id = tokenToPrimitiveTypeId(tok.tag)});
}

static inline AstNode *expressionWithoutStructs(Parser *P)
{
    return expression(P, false);
}

static inline AstNode *expressionWithStructs(Parser *P)
{
    return expression(P, true);
}

static AstNode *member(Parser *P, const Token *begin, AstNode *operand)
{
    AstNode *member;
    u64 flags = flgNone;
    if (match(P, tokQuote))
        flags = flgAnnotation | flgComptime;

    if (check(P, tokIntLiteral))
        member = parseInteger(P, false);
    else if (check(P, tokSubstitutue))
        member = substitute(P, false);
    else if (flags & flgAnnotation) {
        member = parseIdentifier(P);
    }
    else
        member = parsePath(P);

    return newAstNode(
        P,
        begin,
        &(AstNode){.tag = astMemberExpr,
                   .flags = flags,
                   .memberExpr = {.target = operand, .member = member}});
}

static AstNode *indexExpr(Parser *P, AstNode *operand)
{
    Token tok = *consume0(P, tokIndexExpr);
    AstNode *index = expressionWithoutStructs(P);
    consume0(P, tokRBracket);

    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astIndexExpr,
                   .indexExpr = {.target = operand, .index = index}});
}

static AstNode *postfix(Parser *P, AstNode *(parsePrimary)(Parser *, bool))
{
    const Token first = *current(P);
    AstNode *operand = parsePrimary(P, true);
    while (true) {
        switch (current(P)->tag) {
        case tokPlusPlus:
        case tokMinusMinus:
            break;
        case tokDot: {
            const Token tok = *advance(P);
            bool isBuiltin = match(P, tokHash) != NULL;
            operand = member(P, &tok, operand);
            operand->flags |= (isBuiltin ? flgBuiltin : flgNone);
            continue;
        }
        case tokBAndDot: {
            const Token tok = *advance(P);
            operand = newAstNode(
                P,
                &first,
                &(AstNode){
                    .tag = astCallExpr,
                    .callExpr = {
                        .callee = newAstNode(
                            P,
                            &first,
                            &(AstNode){
                                .tag = astMemberExpr,
                                .memberExpr = {
                                    .target = operand,
                                    .member = newAstNode(
                                        P,
                                        &tok,
                                        &(AstNode){.tag = astIdentifier,
                                                   .ident.value =
                                                       S_Redirect})}})}});
            bool isBuiltin = match(P, tokHash) != NULL;
            operand = member(P, &tok, operand);
            operand->flags |= (isBuiltin ? flgBuiltin : flgNone);
            continue;
        }
        case tokIndexExpr: {
            operand = indexExpr(P, operand);
            continue;
        }
        case tokLNot:
            if (checkPeek(P, 1, tokAs))
                operand = postfixCast(P, operand, true);
            else
                operand = macroExpression(P, operand);
            continue;
        case tokLParen:
            operand = callExpression(P, operand);
            continue;
        case tokAs:
        case tokBangColon:
            operand = postfixCast(P, operand, true);
            continue;
        default:
            return operand;
        }

        const TokenTag tag = advance(P)->tag;
        return newAstNode_(
            P,
            &operand->loc,
            &(AstNode){.tag = astUnaryExpr,
                       .unaryExpr = {.operand = operand,
                                     .op = tokenToPostfixUnaryOperator(tag)}});
    }

    unreachable("unreachable");
}

static AstNode *fieldExpr(Parser *P);

static AstNode *structExpr(Parser *P,
                           AstNode *lhs,
                           AstNode *(parseField)(Parser *));

static AstNode *functionParam(Parser *P);

static AstNode *prefix(Parser *P, AstNode *(parsePrimary)(Parser *, bool))
{
    Token start = *current(P);
    bool isRefof = check(P, tokBAnd);
    bool isPtrof = check(P, tokPtrof);
    if (check(P, tokMinus, tokPlus)) {
        if (checkPeek(P, 1, tokIntLiteral)) {
            advance(P);
            return parseInteger(P, previous(P)->tag == tokMinus);
        }
        if (checkPeek(P, 1, tokFloatLiteral)) {
            advance(P);
            return parseFloat(P, previous(P)->tag == tokMinus);
        }
    }

    if (check(P, tokDot) && peek(P, 1)->tag == tokIdent) {
        const Token tok = *advance(P);
        AstNode *member = parseIdentifier(P);
        return newAstNode(
            P,
            &tok,
            &(AstNode){.tag = astMemberExpr, .memberExpr = {.member = member}});
    }

    if (match(P, tokDefined)) {
        const Token tok = *previous(P);
        cstring name = NULL;
        if (match(P, tokLParen)) {
            name = getTokenString(P, consume0(P, tokIdent), false);
            consume0(P, tokRParen);
        }
        else
            name = getTokenString(P, consume0(P, tokIdent), false);

        return newAstNode(P,
                          &tok,
                          &(AstNode){.tag = astBoolLit,
                                     .boolLiteral.value = preprocessorHasMacro(
                                         &P->cc->preprocessor, name, NULL)});
    }

    if (check(P, tokColon))
        return parseSymbol(P);

    if (check(P, tokLaunch))
        return launchExpression(P);

    switch (current(P)->tag) {
#define f(O, T, ...) case tok##T:
        AST_PREFIX_EXPR_LIST(f)
#undef f
        break;
    default:
        return postfix(P, parsePrimary);
    }

    const Token tok = *advance(P);
    AstNode *operand = prefix(P, parsePrimary);

    if (isPtrof) {
        return newAstNode(
            P,
            &tok,
            &(AstNode){.tag = astPointerOf,
                       .unaryExpr = {.operand = operand, .op = opPtrof}});
    }
    else if (isRefof) {
        return newAstNode(P,
                          &tok,
                          &(AstNode){.tag = astReferenceOf,
                                     .unaryExpr = {.operand = operand,
                                                   .op = opRefof,
                                                   .isPrefix = true}});
    }

    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astUnaryExpr,
                   .unaryExpr = {.operand = operand,
                                 .op = tokenToUnaryOperator(tok.tag),
                                 .isPrefix = true}});
}

static AstNode *assign(Parser *P, AstNode *(parsePrimary)(Parser *, bool));
static AstNode *ternary(Parser *P, AstNode *(parsePrimary)(Parser *, bool));

static AstNode *binary(Parser *P,
                       AstNode *lhs,
                       int prec,
                       AstNode *(parsePrimary)(Parser *, bool))
{
    if (lhs == NULL)
        lhs = prefix(P, parsePrimary);

    while (!isEoF(P)) {
        const Token tok = *current(P);
        if (tok.tag == tokMinus && checkPeek(P, 1, tokFunc))
            break;

        Operator op = tokenToBinaryOperator(tok.tag);
        if (op == opInvalid)
            break;

        int nextPrecedence = getBinaryOpPrecedence(op);
        if (nextPrecedence >= prec)
            break;

        advance(P);
        AstNode *rhs;
        if (op == opCatch && match(P, tokDiscard)) {
            rhs = newAstNode(P, previous(P), &(AstNode){.tag = astBlockStmt});
        }
        else
            rhs = binary(P, NULL, nextPrecedence, parsePrimary);
        lhs = newAstNode_(
            P,
            &lhs->loc,
            &(AstNode){.tag = astBinaryExpr,
                       .binaryExpr = {.lhs = lhs, .op = op, .rhs = rhs}});
    }

    return lhs;
}

static AstNode *assign(Parser *P, AstNode *(parsePrimary)(Parser *, bool))
{
    AstNode *lhs = prefix(P, parsePrimary);
    const Token tok = *current(P);
    if (isAssignmentOperator(tok.tag)) {
        advance(P);
        AstNode *rhs = ternary(P, parsePrimary);

        return newAstNode_(
            P,
            &lhs->loc,
            &(AstNode){.tag = astAssignExpr,
                       .assignExpr = {.lhs = lhs,
                                      .op = tokenToAssignmentOperator(tok.tag),
                                      .rhs = rhs}});
    }

    return binary(P, lhs, getMaxBinaryOpPrecedence(), parsePrimary);
}

static AstNode *ternary(Parser *P, AstNode *(parsePrimary)(Parser *, bool))
{
    AstNode *cond = assign(P, parsePrimary);
    if (match(P, tokQuestion)) {
        AstNode *lhs = NULL;
        // allow ?: operator
        if (!match(P, tokColon)) {
            lhs = ternary(P, parsePrimary);
            consume0(P, tokColon);
        }
        AstNode *rhs = ternary(P, parsePrimary);

        return newAstNode_(
            P,
            &cond->loc,
            &(AstNode){
                .tag = astTernaryExpr,
                .ternaryExpr = {.cond = cond, .body = lhs, .otherwise = rhs}});
    }

    return cond;
}

static AstNode *stringExpr(Parser *P)
{
    const Token tok = *consume0(P, tokLString);
    AstNodeList parts = {NULL};
    while (!check(P, tokRString) && !isEoF(P)) {
        if (check(P, tokLStrFmt)) {
            advance(P);
            continue;
        }
        listAddAstNode(&parts, expression(P, false));
    }
    consume0(P, tokRString);
    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astStringExpr, .stringExpr.parts = parts.first});
}

static inline bool maybeClosure(Parser *P)
{
    return (checkPeek(P, 1, tokAt) &&
            checkPeek(P, 2, tokIdent)) // (@param: Type)
           || (checkPeek(P, 1, tokElipsis) && checkPeek(P, 2, tokIdent) &&
               checkPeek(P, 2, tokColon)) // (...a: Type)
           ||
           (checkPeek(P, 1, tokIdent) && checkPeek(P, 2, tokColon)) // (a: Type)
           || (checkPeek(P, 1, tokRParen) &&
               (checkPeek(P, 2, tokColon)         // () : Type
                || checkPeek(P, 2, tokFatArrow))) // () =>
        ;
}

static inline bool maybeAnonymousStruct(Parser *P)
{
    // {} or { x: }
    return (checkPeek(P, 1, tokIdent) && checkPeek(P, 2, tokColon)) ||
           checkPeek(P, 1, tokRBrace);
}

static AstNode *functionParam(Parser *P)
{
    AstNode *attrs = NULL;
    Token tok = *current(P);
    if (check(P, tokAt))
        attrs = attributes(P);

    u64 flags = match(P, tokElipsis) ? flgVariadic : flgNone;
    const char *name = getTokenString(P, consume0(P, tokIdent), false);
    consume0(P, tokColon);
    bool isConst = match(P, tokConst);
    AstNode *type = parseType(P), *def = NULL;
    type->flags |= (isConst ? flgConst : flgNone);

    if (match(P, tokAssign)) {
        def = expression(P, false);
    }

    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astFuncParamDecl,
                   .attrs = attrs,
                   .flags = flags,
                   .funcParam = {.name = name, .type = type, .def = def}});
}

static AstNode *implicitCast(Parser *P)
{
    AstNode *expr;
    Token tok = *consume0(P, tokLess);
    AstNode *to = parseType(P);
    consume0(P, tokGreater);
    expr = expressionWithoutStructs(P);

    return makeAstNode(
        P->memPool,
        &tok.fileLoc,
        &(AstNode){.tag = astCastExpr, .castExpr = {.to = to, .expr = expr}});
}

static AstNode *range(Parser *P)
{
    AstNode *start, *end, *step = NULL;
    bool down = false;
    Token tok = *consume0(P, tokRange);
    consume0(P, tokLParen);
    start = expressionWithoutStructs(P);
    consume0(P, tokComma);
    end = expressionWithoutStructs(P);
    if (match(P, tokComma)) {
        step = expressionWithoutStructs(P);
    }
    if (match(P, tokComma)) {
        consume0(P, tokTrue);
        down = true;
    }
    consume0(P, tokRParen);

    return makeAstNode(P->memPool,
                       &tok.fileLoc,
                       &(AstNode){.tag = astRangeExpr,
                                  .rangeExpr = {.start = start,
                                                .end = end,
                                                .step = step,
                                                .down = down}});
}

static AstNode *funcReturnType(Parser *P)
{
    Token start = *current(P);
    match(P, tokLNot);
    AstNode *ret = parseType(P);
    if (start.tag == tokLNot) {
        ret->next = newAstNode(
            P,
            &start,
            &(AstNode){
                .tag = astPath,
                .path = {.elements = newAstNode(
                             P,
                             &start,
                             &(AstNode){.tag = astPathElem,
                                        .pathElement.name = S_Exception})}});
        ret = newAstNode(
            P,
            &start,
            &(AstNode){.tag = astUnionDecl,
                       .unionDecl = {.members = ret, .isResult = true}});
    }
    return ret;
}

static AstNode *closure(Parser *P)
{
    AstNode *ret = NULL, *body = NULL;
    u64 flags = match(P, tokAsync) ? flgAsync : flgNone;
    Token tok = *consume0(P, tokLParen);
    AstNode *params = parseMany(P, tokRParen, tokComma, functionParam);
    consume0(P, tokRParen);

    if (match(P, tokColon)) {
        ret = funcReturnType(P);
    }
    consume0(P, tokFatArrow);
    if (check(P, tokLBrace))
        body = block(P);
    else
        body = expression(P, true);
    return newAstNode(
        P,
        &tok,
        &(AstNode){
            .tag = astClosureExpr,
            .flags = flags,
            .closureExpr = {.params = params, .ret = ret, .body = body}});
}

static AstNode *async(Parser *P)
{
    Token tok = *consume0(P, tokAsync);
    AstNode *name = NULL;
    if (check(P, tokStringLiteral))
        name = parseString(P);
    else
        name = makeNullLiteral(P->memPool, &tok.fileLoc, NULL, NULL);

    AstNode *body = statement(P, true);
    body->next = name;

    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astMacroCallExpr,
                   .macroCallExpr = {
                       .callee = makeIdentifier(
                           P->memPool, &tok.fileLoc, S___async, 0, NULL, NULL),
                       .args = body}});
}

static AstNode *tuple(
    Parser *P,
    cstring msg,
    bool strict,
    AstNode *(create)(Parser *, const FilePos *, AstNode *, bool),
    AstNode *(with)(Parser *P))
{
    const Token start = *consume0(P, tokLParen);
    AstNode *args = parseMany(P, tokRParen, tokComma, with);
    consume0(P, tokRParen);

    return create(P, &start.fileLoc.begin, args, strict);
}

static AstNode *parseTupleType(Parser *P)
{
    Token tok = *consume0(P, tokLParen);
    AstNode *elems = parseMany(P, tokRParen, tokComma, parseType);
    consume0(P, tokRParen);
    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astTupleType, .tupleType = {.elements = elems}});
}

static AstNode *parseArrayType(Parser *P)
{
    AstNode *type = NULL, *dim = NULL;
    Token tok = *consume0(P, tokLBracket);
    type = parseType(P);
    if (match(P, tokComma)) {
        dim = expressionWithoutStructs(P);
    }
    consume0(P, tokRBracket);
    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astArrayType,
                   .arrayType = {.elementType = type, .dim = dim}});
}

static AstNode *parseGenericParam(Parser *P)
{
    AstNodeList constraints = {NULL};
    bool isVariadic = match(P, tokElipsis) != NULL;
    Token tok = *consume0(P, tokIdent);
    if (match(P, tokColon)) {
        do {
            listAddAstNode(&constraints, parsePath(P));
            if (!match(P, tokBOr))
                break;
        } while (!isEoF(P));
    }

    AstNode *defaultValue = NULL;
    if (!isVariadic && match(P, tokAssign))
        defaultValue = parsePath(P);

    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astGenericParam,
                   .flags = isVariadic ? flgVariadic : flgNone,
                   .genericParam = {.name = getTokenString(P, &tok, false),
                                    .constraints = constraints.first,
                                    .defaultValue = defaultValue}});
}

static AstNode *parseGenericParams(Parser *P)
{
    AstNodeList params = {};
    do {
        AstNode *param = insertAstNode(&params, parseGenericParam(P));
        if (hasFlag(param, Variadic) || check(P, tokRBracket))
            break;
    } while (match(P, tokComma));

    if (check(P, tokRBracket))
        return params.first;

    if (hasFlag(params.last, Variadic)) {
        parserError(P,
                    &current(P)->fileLoc,
                    "variadic template parameter should be the last parameter",
                    NULL);
    }
    else {
        reportUnexpectedToken(P,
                              "expecting a comma ',' or closing bracket ']'");
    }
    unreachable();
}

// async enclosure(a: T, ...b:T[]) -> T;
static AstNode *parseFuncType(Parser *P)
{
    AstNode *gParams = NULL, *params = NULL, *ret = NULL;
    Token tok = *current(P);
    u64 flags = match(P, tokAsync) ? flgAsync : flgNone;
    consume0(P, tokFunc);
    if (match(P, tokLBracket)) {
        gParams = parseGenericParams(P);
        consume0(P, tokRBracket);
    }

    consume0(P, tokLParen);
    params = parseMany(P, tokRParen, tokComma, functionParam);
    consume0(P, tokRParen);

    consume0(P, tokThinArrow);
    ret = funcReturnType(P);

    AstNode *func =
        newAstNode(P,
                   &tok,
                   &(AstNode){.tag = astFuncType,
                              .flags = flags,
                              .funcType = {.params = params, .ret = ret}});
    if (gParams) {
        return newAstNode(
            P,
            &tok,
            &(AstNode){.tag = astGenericDecl,
                       .genericDecl = {.params = gParams, .decl = func}});
    }
    return func;
}

static AstNode *parsePointerType(Parser *P)
{
    Token tok = *consume0(P, tokBXor);
    u64 flags = match(P, tokConst) ? flgConst : flgNone;
    AstNode *pointed = parseType(P);

    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = astPointerType,
                                 .flags = flags,
                                 .pointerType = {.pointed = pointed}});
}

static AstNode *parseReferenceType(Parser *P)
{
    Token tok = *consume0(P, tokBAnd);
    u64 flags = match(P, tokConst) ? flgConst : flgNone;
    AstNode *referred = parseType(P);

    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = astReferenceType,
                                 .flags = flags,
                                 .referenceType = {.referred = referred}});
}

static AstNode *parenExpr(Parser *P, bool strict)
{
    if (maybeClosure(P))
        return closure(P);

    const Token start = *consume0(P, tokLParen);
    bool isSpread = match(P, tokElipsis);
    AstNode *expr = expression(P, true);
    if (!isSpread && match(P, tokRParen)) {
        return newAstNode(
            P, &start, &(AstNode){.tag = astGroupExpr, .groupExpr.expr = expr});
    }

    AstNodeList list = {};
    insertAstNode(&list,
                  isSpread ? newAstNode(P,
                                        &start,
                                        &(AstNode){.tag = astSpreadExpr,
                                                   .spreadExpr.expr = expr})
                           : expr);
    while (match(P, tokComma)) {
        if (check(P, tokRParen))
            break;
        isSpread = match(P, tokElipsis);
        expr = expression(P, true);
        insertAstNode(&list,
                      isSpread ? newAstNode(P,
                                            &start,
                                            &(AstNode){.tag = astSpreadExpr,
                                                       .spreadExpr.expr = expr})
                               : expr);
    }
    consume0(P, tokRParen);
    return newAstNode(
        P,
        &start,
        &(AstNode){.tag = astTupleExpr, .tupleExpr.elements = list.first});
}

static AstNode *macroExpression(Parser *P, AstNode *callee)
{
    AstNode *args = NULL;
    consume0(P, tokLNot);
    if (check(P, tokLParen)) {
        consume0(P, tokLParen);
        args = parseMany(P, tokRParen, tokComma, expressionWithStructs);
        consume0(P, tokRParen);
    }

    if (check(P, tokLBrace)) {
        AstNode *lastArg = block(P);
        if (args == NULL) {
            args = lastArg;
        }
        else {
            getLastAstNode(args)->next = lastArg;
        }
    }

    return newAstNode_(
        P,
        &callee->loc,
        &(AstNode){.tag = astMacroCallExpr,
                   .flags = flgComptime,
                   .macroCallExpr = {.callee = callee, .args = args}});
}

static AstNode *launchExpression(Parser *P)
{
    Token tok = *consume0(P, tokLaunch);
    AstNode *body = NULL;
    bool callStyle = match(P, tokLParen);
    if (check(P, tokLBrace)) {
        body = block(P);
    }
    else {
        body = expression(P, true);
        body = makeExprStmt(P->memPool, &body->loc, flgNone, body, NULL, NULL);
    }

    if (callStyle)
        consume0(P, tokRParen);

    body->next = makeReturnAstNode(
        P->memPool,
        builtinLoc(),
        flgNone,
        makeIntegerLiteral(P->memPool, builtinLoc(), 0, NULL, NULL),
        NULL,
        NULL);
    body = makeBlockStmt(P->memPool, &body->loc, body, NULL, NULL);
    AstNode *arg = newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astClosureExpr, .closureExpr = {.body = body}});

    return newAstNode(
        P,
        &tok,
        &(AstNode){
            .tag = astCallExpr,
            .callExpr = {
                .callee = makePath(
                    P->memPool, &tok.fileLoc, S___thread_launch, flgNone, NULL),
                .args =
                    newAstNode(P,
                               &tok,
                               &(AstNode){.tag = astClosureExpr,
                                          .closureExpr = {.body = body}})}});
}

static AstNode *parseCallArguments(Parser *P)
{
    Token tok = *current(P);
    bool isSpread = match(P, tokElipsis);
    AstNode *expr = expressionWithoutStructs(P);
    return isSpread ? newAstNode(P,
                                 &tok,
                                 &(AstNode){.tag = astSpreadExpr,
                                            .spreadExpr.expr = expr})
                    : expr;
}

static AstNode *callExpression(Parser *P, AstNode *callee)
{
    consume0(P, tokLParen);
    AstNode *args = parseMany(P, tokRParen, tokComma, parseCallArguments);
    consume0(P, tokRParen);

    return newAstNode_(
        P,
        &callee->loc,
        &(AstNode){.tag = astCallExpr,
                   .callExpr = {.callee = callee, .args = args}});
}

static AstNode *block(Parser *P)
{
    AstNodeList stmts = {NULL};
    Token tok = *current(P);
    u64 unsafe = match(P, tokUnsafe) != NULL ? flgUnsafe : flgNone;

    consume0(P, tokLBrace);
    while (!check(P, tokRBrace, tokEoF)) {
        E4C_TRY_BLOCK(
            {
                listAddAstNode(&stmts, statement(P, false));
                match(P, tokSemicolon);
            } E4C_CATCH(ParserException) {
                synchronizeUntil(P, tokRBrace);
                break;
            })
    }
    consume0(P, tokRBrace);

    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = astBlockStmt,
                                 .flags = unsafe,
                                 .blockStmt = {.stmts = stmts.first}});
}

static AstNode *assemblyOutput(Parser *P)
{
    Token tok = *consume0(P, tokStringLiteral);
    cstring constraint = getTokenString(P, &tok, true);
    if (constraint[0] != '=' || constraint[1] == '\0') {
        parserError(P,
                    &tok.fileLoc,
                    "unexpected inline assembly output constraints, expecting "
                    "constraint to start with '=' followed by a character, got "
                    "\"{s}\"",
                    (FormatArg[]){{.s = constraint}});
    }

    consume0(P, tokLParen);
    AstNode *expr = expressionWithoutStructs(P);
    consume0(P, tokRParen);

    if (!nodeIsLeftValue(expr)) {
        parserError(P,
                    &expr->loc,
                    "expecting an L-value for inline assembly output",
                    NULL);
    }
    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astAsmOperand,
                   .asmOperand = {.constraint = constraint, .operand = expr}});
}

static AstNode *assemblyInput(Parser *P)
{
    Token tok = *consume0(P, tokStringLiteral);
    cstring constraint = getTokenString(P, &tok, true);
    if (constraint[0] == '\0') {
        parserError(P,
                    &tok.fileLoc,
                    "unexpected inline assembly input constraints, expecting "
                    "a non empty string, got \"{s}\"",
                    (FormatArg[]){{.s = constraint}});
    }

    consume0(P, tokLParen);
    AstNode *expr = expressionWithoutStructs(P);
    consume0(P, tokRParen);

    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astAsmOperand,
                   .asmOperand = {.constraint = constraint, .operand = expr}});
}

static AstNode *assembly(Parser *P)
{
    AstNodeList stmts = {NULL};
    Token tok = *consume0(P, tokAsm);

    consume0(P, tokLParen);
    cstring template = getTokenString(P, consume0(P, tokStringLiteral), true);
    AstNode *outputs = NULL, *inputs = NULL, *clobbers = NULL, *flags = NULL;
    if (match(P, tokColon)) {
        outputs = parseMany2(P, tokColon, tokRParen, tokComma, assemblyOutput);
    }
    if (match(P, tokColon)) {
        inputs = parseMany2(P, tokColon, tokRParen, tokComma, assemblyInput);
    }
    if (match(P, tokColon)) {
        clobbers = parseMany2(P, tokColon, tokRParen, tokComma, parseString);
    }
    if (match(P, tokColon)) {
        flags = parseMany2(P, tokColon, tokRParen, tokComma, parseString);
    }
    consume0(P, tokRParen);

    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = astAsm,
                                 .inlineAssembly = {.text = template,
                                                    .outputs = outputs,
                                                    .inputs = inputs,
                                                    .clobbers = clobbers,
                                                    .flags = flags}});
}

static AstNode *raiseStmt(Parser *P)
{
    Token tok = *consume0(P, tokRaise);
    AstNode *expr = NULL;
    if (!isEndOfStmt(P))
        expr = expression(P, false);
    else
        match(P, tokSemicolon);
    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astReturnStmt,
                   .returnStmt = {.expr = expr, .isRaise = true}});
}

static AstNode *macroSdlStmt(Parser *P)
{
    Token tok = *consume0(P, tokColon);
    Token name = *consume0(P, tokIdent);
    AstNodeList args = {};
    while (check(P, tokLBrace) || !isEndOfStmt(P)) {
        AstNode *arg = insertAstNode(&args, expression(P, true));
        if (nodeIs(arg, BlockStmt))
            break;
    }

    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astExprStmt,
                   .exprStmt.expr = newAstNode(
                       P,
                       &name,
                       &(AstNode){.tag = astMacroCallExpr,
                                  .macroCallExpr = {
                                      .callee = makeIdentifier(
                                          P->memPool,
                                          &name.fileLoc,
                                          getTokenString(P, &name, false),
                                          0,
                                          NULL,
                                          NULL),
                                      .args = args.first}})});
}

static AstNode *array(Parser *P)
{
    Token tok = *consume0(P, tokLBracket);
    AstNode *elems =
        parseMany(P, tokRBracket, tokComma, expressionWithoutStructs);
    consume0(P, tokRBracket);

    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astArrayExpr, .arrayExpr = {.elements = elems}});
}

static AstNode *parseTypeOrIndex(Parser *P)
{
    if (check(P, tokIntLiteral))
        return parseInteger(P, false);
    return parseType(P);
}

static AstNode *pathElement(Parser *P)
{
    AstNode *args = NULL;
    Token tok = *current(P);
    const char *name = NULL;
    bool isKeyword = false;
    switch (tok.tag) {
    case tokIdent:
        name = getTokenString(P, &tok, false);
        break;
    case tokThis:
        name = S_this;
        isKeyword = true;
        break;
    case tokSuper:
        name = S_super;
        isKeyword = true;
        break;
    case tokThisClass:
        name = S_This;
        isKeyword = true;
        break;
    default:
        reportUnexpectedToken(P,
                              "an identifier or the keywords 'this' / 'super'");
    }
    advance(P);

    if (match(P, tokLBracket)) {
        args = parseMany(P, tokRBracket, tokComma, parseTypeOrIndex);
        consume0(P, tokRBracket);
    }

    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = astPathElem,
                                 .pathElement = {.name = name,
                                                 .args = args,
                                                 .isKeyword = isKeyword}});
}

static AstNode *parsePath(Parser *P)
{
    AstNodeList parts = {NULL};
    Token tok = *current(P);
    if (check(P, tokIdent) && checkPeek(P, 1, tokLNot)) {
        return parseIdentifier(P);
    }

    do {
        listAddAstNode(&parts, pathElement(P));

        if (!check(P, tokDot) || peek(P, 1)->tag != tokIdent)
            break;
        consume0(P, tokDot);
        if (match(P, tokHash)) {
            listAddAstNode(&parts, pathElement(P));
            parts.last->flags |= flgBuiltin;
            break;
        }
    } while (!isEoF(P));

    return newAstNode(
        P, &tok, &(AstNode){.tag = astPath, .path = {.elements = parts.first}});
}

static AstNode *fieldExpr(Parser *P)
{
    Token tok = *consume0(P, tokIdent);
    const char *name = getTokenString(P, &tok, false);
    consume0(P, tokColon);

    AstNode *value = expression(P, true);

    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = astFieldExpr,
                                 .next = NULL,
                                 .fieldExpr = {.name = name, .value = value}});
}

static AstNode *structExpr(Parser *P,
                           AstNode *lhs,
                           AstNode *(parseField)(Parser *))
{
    Token tok = *consume0(P, tokLBrace);
    AstNode *fields = parseMany(P, tokRBrace, tokComma, parseField);
    consume0(P, tokRBrace);

    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astStructExpr,
                   .structExpr = {.left = lhs, .fields = fields}});
}

static AstNode *untypedExpr(Parser *P, bool allowStructs)
{
    AstNode *expr = NULL;
    const Token tok = *consume0(P, tokHash);
    if (match(P, tokHash)) {
        logWarning(P->L,
                   &previous(P)->fileLoc,
                   "multiple `#` expression markers not necessary",
                   NULL);
        while (match(P, tokHash))
            ;
    }

    if (check(P, tokIdent)) {
        expr = parsePath(P);
    }
    else
        expr = parseType(P);

    expr->flags |= flgTypeinfo;
    return expr;
}

static AstNode *substitute(Parser *P, bool allowStructs)
{
    AstNode *expr = NULL;
    const Token tok = *consume0(P, tokSubstitutue);
    if (match(P, tokHash, tokSubstitutue)) {
        parserError(P,
                    &previous(P)->fileLoc,
                    "compile time markers `#` or `#{` cannot be used in "
                    "current context",
                    NULL);
    }

    expr = assign(P, primary);

    consume0(P, tokRBrace);

    expr->flags |= flgComptime;
    return expr;
}

static AstNode *primary_(Parser *P, bool allowStructs)
{
    switch (current(P)->tag) {
    case tokNull:
        return parseNull(P);
    case tokTrue:
    case tokFalse:
        return parseBool(P);
    case tokCharLiteral:
        return parseChar(P);
    case tokIntLiteral:
        return parseInteger(P, false);
    case tokFloatLiteral:
        return parseFloat(P, false);
    case tokStringLiteral:
        return parseString(P);
    case tokLString:
        return stringExpr(P);
    case tokLParen:
        return parenExpr(P, true);
    case tokLBrace:
        if (maybeAnonymousStruct(P))
            return structExpr(P, NULL, fieldExpr);
        return block(P);
    case tokLBracket:
        return array(P);
    case tokHash:
        return untypedExpr(P, allowStructs);
    case tokSubstitutue:
        return substitute(P, allowStructs);
    case tokIdent:
    case tokThis:
    case tokThisClass:
    case tokSuper: {
        AstNode *path = parsePath(P);
        if (allowStructs && check(P, tokLBrace))
            return structExpr(P, path, fieldExpr);
        return path;
    }
    case tokModule: {
        advance(P);
        consume0(P, tokDot);
        AstNode *path = primary_(P, allowStructs);
        path->path.elements->flags |= flgModule;
        return path;
    }
    case tokLess:
        return implicitCast(P);
    case tokRange:
        return range(P);
    case tokAsync:
        return async(P);
    default:
        reportUnexpectedToken(P, "a primary expression");
    }

    unreachable("UNREACHABLE");
}

static AstNode *postfixCast(Parser *P, AstNode *expr, bool allowStructs)
{
    if (check(P, tokAs)) {
        advance(P);
        AstNode *type = parseType(P);
        expr = newAstNode_(P,
                           &expr->loc,
                           &(AstNode){.tag = astCastExpr,
                                      .castExpr = {.expr = expr, .to = type}});
    }
    else if (check(P, tokLNot) && checkPeek(P, 1, tokAs)) {
        advance(P);
        advance(P);
        AstNode *type = parseType(P);
        expr = newAstNode_(P,
                           &expr->loc,
                           &(AstNode){.tag = astCastExpr,
                                      .flags = flgBitUnionCast,
                                      .castExpr = {.expr = expr, .to = type}});
    }
    else if (match(P, tokBangColon)) {
        AstNode *type = parseType(P);
        expr =
            newAstNode_(P,
                        &expr->loc,
                        &(AstNode){.tag = astTypedExpr,
                                   .typedExpr = {.expr = expr, .type = type}});
    }
    return expr;
}

static AstNode *primary(Parser *P, bool allowStructs)
{
    Token tok = *current(P);
    return postfixCast(P, primary_(P, allowStructs), allowStructs);
}

static AstNode *expression(Parser *P, bool allowStructs)
{
    AstNode *attrs = NULL;
    if (check(P, tokAt))
        attrs = attributes(P);

    AstNode *expr = ternary(P, primary);
    expr->attrs = attrs;
    return expr;
}

static AstNode *attribute(Parser *P)
{
    Token tok = *consume0(P, tokIdent);
    const char *name = getTokenString(P, &tok, false);
    AstNodeList args = {NULL};
    bool isKvp = false;
    if (match(P, tokLParen) && !isEoF(P)) {
        isKvp = check(P, tokIdent) && checkPeek(P, 1, tokColon);
        while (!check(P, tokRParen, tokEoF)) {
            AstNode *value = NULL;
            const char *pname = NULL;
            Token start = *peek(P, 0);
            if (isKvp) {
                consume0(P, tokIdent);
                pname = getTokenString(P, &start, false);
                consume0(P, tokColon);
            }

            switch (current(P)->tag) {
            case tokTrue:
            case tokFalse:
                value = parseBool(P);
                break;
            case tokCharLiteral:
                value = parseChar(P);
                break;
            case tokIntLiteral:
                value = parseInteger(P, false);
                break;
            case tokFloatLiteral:
                value = parseFloat(P, false);
                break;
            case tokStringLiteral:
                value = parseString(P);
                break;
            case tokPlus:
            case tokMinus:
                if (match(P, tokIntLiteral)) {
                    value = parseInteger(P, previous(P)->tag == tokMinus);
                    break;
                }
                if (check(P, tokFloatLiteral)) {
                    value = parseFloat(P, previous(P)->tag == tokMinus);
                    break;
                }
                // fall through
            default:
                reportUnexpectedToken(P, "string/float/int/char/bool literal");
            }

            if (isKvp) {
                listAddAstNode(
                    &args,
                    newAstNode(P,
                               &start,
                               &(AstNode){.tag = astFieldExpr,
                                          .fieldExpr = {.name = pname,
                                                        .value = value}}));
            }
            else {
                listAddAstNode(&args, value);
            }

            match(P, tokComma);
        }
        consume0(P, tokRParen);
    }

    return newAstNode(
        P,
        &tok,
        &(AstNode){
            .tag = astAttr,
            .attr = {.name = name, .args = args.first, .kvpArgs = isKvp}});
}

static AstNode *attributes(Parser *P)
{
    Token tok = *consume0(P, tokAt);
    AstNode *attrs;
    if (match(P, tokLBracket)) {
        attrs =
            parseAtLeastOne(P, "attribute", tokRBracket, tokComma, attribute);

        consume0(P, tokRBracket);
    }
    else {
        attrs = attribute(P);
    }

    return attrs;
}

static AstNode *define(Parser *P)
{
    Token tok = *consume0(P, tokDefine);
    u64 flags = flgNone;
    if (match(P, tokPub))
        flags |= flgPublic;

    AstNode *names, *type = NULL;
    if (match(P, tokLParen)) {
        names = parseMany(P, tokRParen, tokComma, parseIdentifierWithAlias);
        consume0(P, tokRParen);
    }
    else {
        names = parseIdentifier(P);
    }

    consume0(P, tokColon);
    type = parseType(P);
    type->flags |= flgExtern;

    AstNode *container = NULL;
    if (match(P, tokAs))
        container = parseIdentifier(P);

    return newAstNode(
        P,
        &tok,
        &(AstNode){
            .tag = astDefine,
            .flags = flags,
            .define = {.names = names, .type = type, .container = container}});
}

static AstNode *parseVarDeclName(Parser *P)
{
    if (check(P, tokSubstitutue)) {
        return substitute(P, false);
    }

    return parseIdentifier(P);
}

static AstNode *parseMultipleVariables(Parser *P)
{
    AstNodeList nodes = {NULL};
    do {
        listAddAstNode(&nodes, parseVarDeclName(P));
    } while (match(P, tokComma));

    return nodes.first;
}

static AstNode *variable(
    Parser *P, bool isPublic, bool isExtern, bool isExpression, bool woInit)
{
    Token tok = *current(P);
    uint64_t flags = isPublic ? flgPublic : flgNone;
    flags |= isExtern ? flgExtern : flgNone;
    flags |= tok.tag == tokConst ? flgConst : flgNone;
    bool isComptime = previous(P)->tag == tokHash ||
                      previous(P)->tag == tokSubstitutue ||
                      previous(P)->tag == tokAstMacroAccess;

    if (!match(P, tokConst, tokVar))
        reportUnexpectedToken(P, "var/const to start variable declaration");

    AstNode *names = NULL, *type = NULL, *init = NULL;
    names = isComptime ? parseIdentifier(P) : parseMultipleVariables(P);

    if (!isExpression && (match(P, tokColon) != NULL))
        type = parseType(P);

    if (!isExtern && !woInit) {
        if (tok.tag == tokConst)
            consume0(P, tokAssign);
        if (tok.tag == tokConst || match(P, tokAssign) || isExpression)
            init = expression(P, true);
    }

    if (!(isExpression || woInit || isExtern)) {
        if (init && init->tag == astClosureExpr)
            match(P, tokSemicolon);
        else if (isEndOfStmt(P))
            match(P, tokSemicolon);
        else
            consume(P,
                    tokSemicolon,
                    "';', semicolon required after non-expression variable "
                    "declaration",
                    NULL);
    }

    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astVarDecl,
                   .flags = flags,
                   .varDecl = {.names = names, .type = type, .init = init}});
}

static AstNode *forVariable(Parser *P, bool isComptime)
{
    Token tok = *current(P);
    uint64_t flags = tok.tag == tokConst ? flgConst : flgNone;

    if (isComptime && !check(P, tokConst))
        reportUnexpectedToken(P,
                              "unexpect token, comptime `for` variable can "
                              "only be declared as `const`");

    if (!match(P, tokConst, tokVar))
        reportUnexpectedToken(P, "var/const to start variable declaration");

    AstNode *names = NULL, *type = NULL, *init = NULL;
    names = isComptime
                ? parseIdentifier(P)
                : parseAtLeastOne(
                      P, "variable names", tokColon, tokComma, parseIdentifier);

    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astVarDecl,
                   .flags = flags,
                   .varDecl = {.names = names, .type = type, .init = init}});
}

static AstNode *macroDecl(Parser *P, bool isPublic)
{
    AstNode *params = NULL, *body = NULL, *ret = NULL;
    Token tok = *current(P);
    consume0(P, tokMacro);
    cstring name = getTokenString(P, consume0(P, tokIdent), false);
    u64 flags = isPublic ? flgPublic : flgNone;

    if (match(P, tokLParen)) {
        AstNodeList nodes = {};
        while (!check(P, tokEoF, tokRParen)) {
            AstNode *param = insertAstNode(&nodes, parseMacroParam(P));
            if (!match(P, tokComma) && !check(P, tokRParen)) {
                parserError(
                    P,
                    &current(P)->fileLoc,
                    "unexpected token '{s}', expecting ',' or ')'",
                    (FormatArg[]){{.s = token_tag_to_str(current(P)->tag)}});
            }

            if (hasFlag(param, Variadic)) {
                flags |= flgVariadic;
                break;
            }
        }
        params = nodes.first;
        consume0(P, tokRParen);
    }

    if (check(P, tokAssign) && peek(P, tokLParen)) {
        consume0(P, tokAssign);
        body = expression(P, true);
    }
    else if (match(P, tokLParen)) {
        body = parseManyNoSeparator(P, tokRParen, statementOnly);
        consume0(P, tokRParen);
    }
    else {
        body = statementOrExpression(P);
    }

    return newAstNode(
        P,
        &tok,
        &(AstNode){
            .tag = astMacroDecl,
            .flags = flags,
            .macroDecl = {.name = name, .params = params, .body = body}});
}

static AstNode *testContextDeclaration(Parser *P)
{
    AstNode *node = comptimeDeclaration(P);
    node->flags |= flgTestContext;
    return node;
}

static void skipTestDecl(Parser *P)
{
    match(P, tokStringLiteral);
    Token tok = *previous(P);
    consume0(P, tokLBrace);
    i64 count = 1;
    while (!check(P, tokEoF) && count > 0) {
        TokenTag tag = current(P)->tag;
        if (tag == tokLBrace)
            count++;
        else if (tag == tokRBrace)
            count--;

        advance(P);
    }

    if (count != 0)
        parserError(P, &tok.fileLoc, "test case not properly terminated", NULL);
}

static AstNode *testDecl(Parser *P)
{
    AstNode *params = NULL, *body = NULL;
    consume0(P, tokTest);
    Token tok = *current(P);
    if (!P->testMode) {
        skipTestDecl(P);
        return NULL;
    }

    if (match(P, tokLBrace)) {
        AstNode *decls =
            parseManyNoSeparator(P, tokRBrace, testContextDeclaration);
        consume0(P, tokRBrace);
        return decls;
    }
    Token name = *consume0(P, tokStringLiteral);

    body = block(P);
    if (body->blockStmt.stmts == NULL)
        parserError(P, &body->loc, "empty test block not allowed", NULL);

    return newAstNode(
        P,
        &tok,
        &(AstNode){
            .tag = astTestDecl,
            .testDecl = {.name = getStringLiteral(P, &name), .body = body}});
}

static AstNode *exceptionDecl(Parser *P, u64 flags)
{
    AstNode *params = NULL, *body = NULL;
    Token tok = *current(P);
    consume0(P, tokException);
    Token name = *consume0(P, tokIdent);

    if (match(P, tokLParen)) {
        params = parseMany(P, tokRParen, tokComma, functionParam);
        consume0(P, tokRParen);
    }
    if (match(P, tokFatArrow)) {
        body = expression(P, true);
    }
    else
        body = block(P);
    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astException,
                   .flags = flags,
                   .exception = {.name = getTokenString(P, &name, false),
                                 .params = params,
                                 .body = body}});
}

typedef CxyPair(Operator, cstring) OperatorOverload;

static OperatorOverload operatorOverload(Parser *P)
{
    OperatorOverload op = {.f = opInvalid};
    consume0(P, tokQuote);
    if (match(P, tokLBracket)) {
        consume0(P, tokRBracket);
        if (match(P, tokAssign)) {
            op = (OperatorOverload){.f = opIndexAssignOverload,
                                    .s = S_IndexAssignOverload};
        }
        else {
            op = (OperatorOverload){.f = opIndexOverload, .s = S_IndexOverload};
        }
    }
    else if (match(P, tokLParen)) {
        op = (OperatorOverload){.f = opCallOverload, .s = S_CallOverload};
        consume0(P, tokRParen);
    }
    else if (match(P, tokBAndDot)) {
        op = (OperatorOverload){.f = opRedirect, .s = S_Redirect};
    }
    else if (match(P, tokIdent)) {
        Token ident = *previous(P);
        cstring name = getTokenString(P, &ident, false);
        bool builtins = !isBuiltinsInitialized();
        if (name == S_StringOverload_) {
            op = (OperatorOverload){.f = opStringOverload,
                                    .s = S_StringOverload};
        }
        else if (name == S_InitOverload_) {
            op = (OperatorOverload){.f = opInitOverload, .s = S_InitOverload};
        }
        else if (name == S_DeinitOverload_) {
            op = (OperatorOverload){.f = opDeinitOverload,
                                    .s = S_DeinitOverload};
        }
        else if (name == S_DestructorOverload_) {
            op = (OperatorOverload){.f = opDestructorOverload,
                                    .s = S_DestructorOverload};
        }
        else if (builtins && name == S_DestructorFwd_) {
            op = (OperatorOverload){.f = opDestructorFwd, .s = S_DestructorFwd};
        }
        else if (name == S_HashOverload_) {
            op = (OperatorOverload){.f = opHashOverload, .s = S_HashOverload};
        }
        else if (name == S_Deref_) {
            op = (OperatorOverload){.f = opDeref, .s = S_Deref};
        }
        else if (name == S_CopyOverload_) {
            op = (OperatorOverload){.f = opCopyOverload, .s = S_CopyOverload};
        }

        if (op.f == opInvalid) {
            parserError(P,
                        &ident.fileLoc,
                        "unexpected operator overload `{s}`",
                        (FormatArg[]){{.s = name}});
        }
    }
    else {
        switch (current(P)->tag) {
        case tokLNot:
            if (checkPeek(P, 1, tokLNot)) {
                op = (OperatorOverload){.f = opTruthy, .s = S_Truthy};
                advance(P);
            }
            else
                op = (OperatorOverload){.f = opNot, .s = S_Not};
            break;
        case tokAwait:
            op = (OperatorOverload){.f = opAwait, .s = S_Await};
            break;
#define f(O, PP, T, S, N)                                                      \
    case tok##T:                                                               \
        op = (OperatorOverload){.f = op##O, .s = S_##O};                       \
        break;
            AST_BINARY_EXPR_LIST(f);

#undef f
        default:
            reportUnexpectedToken(P, "a binary operator to overload");
        }
        advance(P);
    }
    consume0(P, tokQuote);
    return op;
}

static AstNode *funcDecl(Parser *P, u64 flags)
{
    AstNode *gParams = NULL, *params = NULL, *ret = NULL, *body = NULL;
    Token tok = *current(P);
    bool Extern = (flags & flgExtern) == flgExtern;
    bool Virtual = (flags & flgVirtual) == flgVirtual;
    bool Member = (flags & flgMember) == flgMember;
    flags &= ~flgMember; // Clear member flags, a function might be static
    flags |= match(P, tokAsync) ? flgAsync : flgNone;
    consume0(P, tokFunc);
    cstring name = NULL;
    Operator op = opInvalid;
    if (Member && check(P, tokQuote)) {
        OperatorOverload overload = operatorOverload(P);
        op = overload.f;
        name = overload.s;
    }
    else {
        name = getTokenString(P, consume0(P, tokIdent), false);
    }

    if (match(P, tokLBracket)) {
        if (Extern)
            reportUnexpectedToken(
                P, "a '(', extern functions cannot have generic parameters");
        if (Virtual)
            reportUnexpectedToken(
                P, "a '(', virtual functions cannot have generic parameters");

        gParams = parseGenericParams(P);
        consume0(P, tokRBracket);
    }

    consume0(P, tokLParen);
    params = parseMany(P, tokRParen, tokComma, functionParam);
    consume0(P, tokRParen);

    if (match(P, tokColon)) {
        ret = funcReturnType(P);
    }
    else if (Extern)
        reportUnexpectedToken(P,
                              "colon before function declaration return type");

    if (!Extern) {
        if (match(P, tokFatArrow)) {
            body = expression(P, true);
            match(P, tokSemicolon);
        }
        else if (check(P, tokLBrace)) {
            body = block(P);
        }
        else if (Virtual) {
            if (ret == NULL)
                reportUnexpectedToken(
                    P, "':' before virtual function declaration return type");
            else
                flags |= flgAbstract;
        }
        else {
            reportUnexpectedToken(P, "a function body");
        }
    }
    else {
        Token tmp = *current(P);
        if (match(P, tokFatArrow, tokLBrace)) {
            parserError(P,
                        &tmp.fileLoc,
                        "extern functions cannot be declared with a body",
                        NULL);
        }
        // make semi-colon optional
        match(P, tokSemicolon);
    }

    AstNode *func = newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astFuncDecl,
                   .flags = flags,
                   .funcDecl = {
                       .name = name,
                       .operatorOverload = op,
                       .signature = makeFunctionSignature(
                           P->memPool,
                           &(FunctionSignature){.params = params, .ret = ret}),
                       .body = body}});
    if (gParams) {
        return newAstNode(
            P,
            &tok,
            &(AstNode){.tag = astGenericDecl,
                       .flags = flags,
                       .genericDecl = {.params = gParams, .decl = func}});
    }
    return func;
}

static AstNode *ifStatement(Parser *P)
{
    AstNode *ifElse = NULL, *cond = NULL, *body = NULL;
    Token tok = *consume0(P, tokIf);
    consume0(P, tokLParen);
    if (check(P, tokConst, tokVar)) {
        cond = variable(P, false, false, true, false);
    }
    else {
        cond = expression(P, true);
    }
    consume0(P, tokRParen);

    body = statement(P, false);
    if (match(P, tokElse)) {
        ifElse = statement(P, false);
    }

    return newAstNode(
        P,
        &tok,
        &(AstNode){
            .tag = astIfStmt,
            .ifStmt = {.cond = cond, .body = body, .otherwise = ifElse}});
}

static AstNode *forStatement(Parser *P, bool isComptime)
{
    AstNode *body = NULL;

    Token tok = *consume0(P, tokFor);

    consume0(P, tokLParen);
    AstNode *var = forVariable(P, isComptime);
    consume0(P, tokColon);
    AstNode *range = expression(P, true);
    consume0(P, tokRParen);
    if (!match(P, tokSemicolon))
        body = statement(P, false);

    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astForStmt,
                   .forStmt = {.var = var, .range = range, .body = body}});
}

static AstNode *whileStatement(Parser *P)
{
    AstNode *body = NULL;
    AstNode *cond = NULL;

    Token tok = *consume0(P, tokWhile);
    if (check(P, tokLBrace)) {
        cond = newAstNode(
            P, &tok, &(AstNode){.tag = astBoolLit, .boolLiteral.value = true});
        body = statement(P, false);
    }
    else {
        consume0(P, tokLParen);
        if (check(P, tokConst, tokVar)) {
            cond = variable(P, false, false, true, false);
        }
        else {
            cond = expression(P, true);
        }
        consume0(P, tokRParen);
        if (!match(P, tokSemicolon))
            body = statement(P, false);
    }

    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = astWhileStmt,
                                 .whileStmt = {.cond = cond, .body = body}});
}

static AstNode *caseStatement(Parser *P)
{
    u64 flags = flgNone;
    Token tok = *current(P);
    AstNode *match = NULL, *body = NULL;
    if (match(P, tokCase) || previous(P)->tag == tokComma) {
        P->inCase = true;
        match = expression(P, false);
        P->inCase = false;
    }
    else {
        consume(
            P, tokDefault, "expecting a 'default' or a 'case' statement", NULL);
        flags |= flgDefault;
    }

    if (!match(P, tokComma)) {
        consume0(P, tokFatArrow);
        if (!check(P, tokCase, tokRBrace)) {
            body = statement(P, false);
        }
    }

    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = astCaseStmt,
                                 .flags = flags,
                                 .caseStmt = {.match = match, .body = body}});
}

static AstNode *switchStatement(Parser *P)
{
    AstNodeList cases = {NULL};
    Token tok = *consume0(P, tokSwitch);

    consume0(P, tokLParen);
    AstNode *cond = expression(P, false);
    consume0(P, tokRParen);

    consume0(P, tokLBrace);
    while (!check(P, tokRBrace, tokEoF)) {
        listAddAstNode(&cases, comptime(P, caseStatement));
    }
    consume0(P, tokRBrace);

    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astSwitchStmt,
                   .switchStmt = {.cond = cond, .cases = cases.first}});
}

static AstNode *matchCaseStatement(Parser *P)
{
    u64 flags = flgNone;
    Token tok = *current(P);
    AstNode *match = NULL, *body = NULL;
    AstNode *variable = NULL;
    bool isMulti = previous(P)->tag == tokComma;
    if (match(P, tokCase) || isMulti) {
        P->inCase = true;
        match = parseType(P);
        P->inCase = false;
        if (!isMulti && !check(P, tokComma) && match(P, tokAs)) {
            tok = *current(P);
            bool isReference = match(P, tokBAnd);
            variable = parseIdentifier(P);
            variable =
                newAstNode(P,
                           &tok,
                           &(AstNode){.tag = astVarDecl,
                                      .varDecl = {.name = variable->ident.value,
                                                  .names = variable}});
            variable->flags |= (isReference ? flgReference : flgNone);
        }
    }
    else {
        consume(P, tokElse, "expecting an 'else' or a 'case' statement", NULL);
        flags |= flgDefault;
    }

    if (!match(P, tokComma)) {
        // either case X { or case =>
        if (!check(P, tokLBrace))
            consume0(P, tokFatArrow);
        if (!check(P, tokCase, tokRBrace)) {
            body = statement(P, false);
        }
    }

    return newAstNode(
        P,
        &tok,
        &(AstNode){
            .tag = astCaseStmt,
            .flags = flags,
            .caseStmt = {.match = match, .body = body, .variable = variable}});
}

static AstNode *matchStatement(Parser *P)
{
    AstNodeList cases = {NULL};
    Token tok = *consume0(P, tokMatch);

    consume0(P, tokLParen);
    AstNode *expr = expression(P, false);
    consume0(P, tokRParen);

    consume0(P, tokLBrace);
    while (!check(P, tokRBrace, tokEoF)) {
        listAddAstNode(&cases, comptime(P, matchCaseStatement));
    }
    consume0(P, tokRBrace);

    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astMatchStmt,
                   .matchStmt = {.expr = expr, .cases = cases.first}});
}

static AstNode *deferStatement(Parser *P)
{
    AstNode *expr = NULL;
    Token tok = *consume0(P, tokDefer);
    bool isBlock = check(P, tokLBrace) != NULL;
    expr = expression(P, true);
    if (!isBlock)
        match(P, tokSemicolon);

    return newAstNode(
        P, &tok, &(AstNode){.tag = astDeferStmt, .deferStmt = {.stmt = expr}});
}

static AstNode *returnStatement(Parser *P)
{
    AstNode *expr = NULL;
    Token tok = *consume0(P, tokReturn);
    if (!isEndOfStmt(P)) {
        expr = expression(P, true);
    }
    match(P, tokSemicolon);

    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astReturnStmt, .returnStmt = {.expr = expr}});
}

static AstNode *yieldStatement(Parser *P)
{
    AstNode *expr = NULL;
    Token tok = *consume0(P, tokYield);
    expr = expression(P, true);
    match(P, tokSemicolon);

    return newAstNode(
        P, &tok, &(AstNode){.tag = astYieldStmt, .yieldStmt = {.expr = expr}});
}

static AstNode *continueStatement(Parser *P)
{
    Token tok = *current(P);
    if (!match(P, tokBreak, tokContinue)) {
        reportUnexpectedToken(P, "continue/break");
    }

    match(P, tokSemicolon);
    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = tok.tag == tokContinue ? astContinueStmt
                                                               : astBreakStmt});
}

static AstNode *statement(Parser *P, bool exprOnly)
{
    AstNode *attrs = NULL;
    u64 flags = flgNone;
    if (check(P, tokAt))
        attrs = attributes(P);

    bool isComptime = match(P, tokHash) != NULL;
    if (isComptime && !check(P, tokIf, tokFor, tokWhile, tokSwitch, tokConst)) {
        parserError(P,
                    &current(P)->fileLoc,
                    "current token is not a valid compile time token",
                    NULL);
    }

    AstNode *stmt = NULL;

    switch (current(P)->tag) {
    case tokIf:
        stmt = ifStatement(P);
        break;
    case tokFor:
        stmt = forStatement(P, isComptime);
        break;
    case tokSwitch:
        stmt = switchStatement(P);
        break;
    case tokMatch:
        stmt = matchStatement(P);
        break;
    case tokWhile:
        stmt = whileStatement(P);
        break;
    case tokDefer:
        stmt = deferStatement(P);
        break;
    case tokReturn:
        stmt = returnStatement(P);
        break;
    case tokYield:
        stmt = yieldStatement(P);
        break;
    case tokBreak:
    case tokContinue:
        stmt = continueStatement(P);
        break;
    case tokVar:
    case tokConst:
        stmt = variable(P, false, false, false, false);
        break;
    case tokFunc:
        stmt = funcDecl(P, flgNone);
        break;
    case tokLBrace:
        stmt = block(P);
        break;
    case tokAsm:
        stmt = assembly(P);
        break;
    case tokRaise:
        stmt = raiseStmt(P);
        break;
    case tokColon:
        stmt = macroSdlStmt(P);
        break;
    default: {
        AstNode *expr = expression(P, false);
        stmt = exprOnly ? expr
                        : newAstNode_(P,
                                      &expr->loc,
                                      &(AstNode){.tag = astExprStmt,
                                                 .exprStmt = {.expr = expr}});
        match(P, tokSemicolon);
        break;
    }
    }

    stmt->attrs = attrs;
    stmt->flags |= (isComptime ? flgComptime : flgNone) | flags;
    return stmt;
}

static AstNode *xFormType(Parser *P)
{
    // `#T as _, t => (cond, )? &t.Context`
    Token tok = *consume0(P, tokQuote);
    AstNode *tupleType = parseType(P);
    consume0(P, tokAs);
    Token *varToken = consume0(P, tokIdent);
    AstNode *args =
        newAstNode(P,
                   varToken,
                   &(AstNode){.tag = astVarDecl,
                              .flags = flgComptime,
                              .varDecl = {
                                  .name = getTokenString(P, varToken, false),
                              }});
    if (match(P, tokComma)) {
        varToken = consume0(P, tokIdent);
        args->next = newAstNode(
            P,
            varToken,
            &(AstNode){.tag = astVarDecl,
                       .flags = flgComptime,
                       .varDecl = {
                           .name = getTokenString(P, varToken, false),
                       }});
    }
    consume0(P, tokFatArrow);
    AstNode *xForm = parseType(P);
    AstNode *cond = NULL;
    if (match(P, tokComma)) {
        cond = expression(P, false);
        cond->flags |= flgComptime;
    }
    consume0(P, tokQuote);

    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = astTupleXform,
                                 .flags = flgComptime,
                                 .xForm = {.target = tupleType,
                                           .args = args,
                                           .cond = cond,
                                           .xForm = xForm}});
}

static AstNode *xFormExpr(Parser *P)
{
    // `expr : T, i => xform (,cond)?`
    Token tok = *consume0(P, tokQuote);
    AstNode *expr = expression(P, true);
    consume0(P, tokColon);
    Token *varToken = consume0(P, tokIdent);
    AstNode *args =
        newAstNode(P,
                   varToken,
                   &(AstNode){.tag = astVarDecl,
                              .flags = flgComptime,
                              .varDecl = {
                                  .name = getTokenString(P, varToken, false),
                              }});
    if (match(P, tokComma)) {
        varToken = consume0(P, tokIdent);
        args->next = newAstNode(
            P,
            varToken,
            &(AstNode){.tag = astVarDecl,
                       .flags = flgComptime,
                       .varDecl = {
                           .name = getTokenString(P, varToken, false),
                       }});
    }
    consume0(P, tokFatArrow);
    AstNode *xForm = expression(P, true);
    AstNode *cond = NULL;
    if (match(P, tokComma)) {
        cond = expression(P, false);
        cond->flags |= flgComptime;
    }
    consume0(P, tokQuote);

    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = astTupleXform,
                                 .flags = flgComptime,
                                 .xForm = {.target = expr,
                                           .args = args,
                                           .cond = cond,
                                           .xForm = xForm}});
}

static AstNode *parseTypeImpl(Parser *P)
{
    AstNode *type;
    FileLoc loc = current(P)->fileLoc;
    bool isConst = match(P, tokConst);
    Token tok = *current(P);
    if (isPrimitiveType(tok.tag)) {
        type = primitive(P);
    }
    else {
        switch (tok.tag) {
        case tokIdent:
        case tokThisClass:
            type = parsePath(P);
            if (nodeIs(type, Path))
                type->path.isType = true;
            break;
        case tokLParen:
            type = parseTupleType(P);
            break;
        case tokLBracket:
            type = parseArrayType(P);
            break;
        case tokAsync:
        case tokFunc:
            type = parseFuncType(P);
            break;
        case tokBXor:
            type = parsePointerType(P);
            break;
        case tokBAnd:
            type = parseReferenceType(P);
            break;
        case tokVoid:
            advance(P);
            type =
                makeAstNode(P->memPool, &loc, &(AstNode){.tag = astVoidType});
            break;
        case tokString:
            advance(P);
            type =
                makeAstNode(P->memPool, &loc, &(AstNode){.tag = astStringType});
            break;
        case tokCChar:
            advance(P);
            type = makeAstNode(P->memPool,
                               &loc,
                               &(AstNode){.tag = astPrimitiveType,
                                          .primitiveType.id = prtCChar});
            break;
        case tokSubstitutue:
            type = substitute(P, false);
            break;
        case tokAuto:
            advance(P);
            type =
                makeAstNode(P->memPool, &loc, &(AstNode){.tag = astAutoType});
            break;
        case tokQuote:
            type = xFormType(P);
            break;
        default:
            reportUnexpectedToken(P, "a type");
            unreachable("");
        }
    }

    type->loc.begin = loc.begin;
    if (match(P, tokQuestion)) {
        type = makeAstNode(
            P->memPool,
            &loc,
            &(AstNode){.tag = astOptionalType, .optionalType.type = type});
    }

    type->flags |= flgTypeAst | (isConst ? flgConst : flgNone);
    return type;
}

static AstNode *parseType(Parser *P)
{
    AstNodeList members = {0};
    Token tok = *current(P);
    do {
        listAddAstNode(&members, parseTypeImpl(P));
    } while (match(P, tokBOr));

    if (members.first != members.last) {
        return newAstNode(P,
                          &tok,
                          &(AstNode){.tag = astUnionDecl,
                                     .flags = flgNone,
                                     .unionDecl = {.members = members.first}});
    }
    else {
        return members.first;
    }
}

static AstNode *parseStructField(Parser *P, bool isPrivate)
{
    AstNode *type = NULL, *value = NULL;
    Token tok = *consume0(P, tokIdent);
    cstring name = getTokenString(P, &tok, false);
    u32 bits = 0;
    if (check(P, tokColon) && checkPeek(P, 1, tokIntLiteral)) {
        consume0(P, tokColon);
        bits = consume0(P, tokIntLiteral)->uVal;
        if (bits == 0 || bits > 64) {
            logError(P->L,
                     &previous(P)->fileLoc,
                     "struct bit size of {u32} out of range, valid range "
                     "is 1-64 ",
                     (FormatArg[]){{.u32 = bits}});
        }
    }

    if (match(P, tokColon)) {
        type = parseType(P);
    }

    if (type == NULL || check(P, tokAssign)) {
        consume0(P, tokAssign);
        value = expression(P, false);
    }
    // consume optional semicolon
    match(P, tokSemicolon);

    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = astFieldDecl,
                                 .flags = isPrivate ? flgPrivate : flgNone,
                                 .structField = {.name = name,
                                                 .bits = bits,
                                                 .type = type,
                                                 .value = value}});
}

static AstNode *parseClassOrStructMember(Parser *P)
{
    AstNode *member = NULL, *attrs = NULL;
    Token tok = *current(P);

    if (check(P, tokAt))
        attrs = attributes(P);

    bool isPrivate = match(P, tokMinus);
    bool isVirtual = match(P, tokVirtual);
    bool isConst = match(P, tokConst);

    switch (current(P)->tag) {
    case tokIdent:
        if (checkPeek(P, 1, tokLNot))
            member = parseIdentifier(P);
        else if (checkPeek(P, 1, tokDot))
            member = macroExpression(P, parsePath(P));
        else
            member = parseStructField(P, isPrivate);
        break;
    case tokFunc:
    case tokAsync: {
        u64 flags = isVirtual ? flgVirtual : flgNone;
        flags |= isPrivate ? flgNone : flgPublic;
        flags |= isConst ? flgConst : flgNone;
        member = funcDecl(P, flags | flgMember);
        break;
    }
    case tokMacro:
        if (attrs)
            parserError(P,
                        &tok.fileLoc,
                        "attributes cannot be attached to macro declarations",
                        NULL);
        member = macroDecl(P, !isPrivate);
        break;
    case tokType:
        member = aliasDecl(P, !isPrivate, false);
        break;
    case tokStruct:
        member = classOrStructDecl(P, !isPrivate, false);
        break;
    default:
        reportUnexpectedToken(P, "struct member");
    }
    member->flags |= (isConst ? flgConst : flgNone);
    member->attrs = attrs;
    return member;
}

static AstNode *comptimeClassOrStructDecl(Parser *P)
{
    return comptime(P, parseClassOrStructMember);
}

static AstNode *parseInterfaceMember(Parser *P)
{
    AstNode *member = NULL, *attrs = NULL;
    Token tok = *current(P);

    if (check(P, tokAt))
        attrs = attributes(P);

    bool isPrivate = match(P, tokMinus);
    bool isConst = match(P, tokConst);

    switch (current(P)->tag) {
    case tokFunc:
    case tokAsync:
        member = funcDecl(P, isPrivate ? flgExtern : flgPublic | flgExtern);
        break;
    default:
        reportUnexpectedToken(P, "interface member");
    }
    member->flags |= (isConst ? flgConst : flgNone);
    member->attrs = attrs;
    return member;
}

static AstNode *comptimeBlock(Parser *P, AstNode *(*parser)(Parser *))
{
    Token tok = *consume0(P, tokLBrace);
    AstNodeList nodes = {};
    // parseManyNoSeparator(P, tokRBrace, parser);
    while (!check(P, tokRBrace)) {
        insertAstNode(&nodes, comptime(P, parser));
    }
    consume0(P, tokRBrace);
    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = astBlockStmt,
                                 .flags = flgComptime,
                                 .blockStmt.stmts = nodes.first});
}

static AstNode *parseComptimeIf(Parser *P, AstNode *(*parser)(Parser *))
{
    AstNode *cond;
    Token tok = *consume0(P, tokIf);
    consume0(P, tokLParen);
    if (check(P, tokConst, tokVar)) {
        cond = variable(P, false, false, true, false);
    }
    else {
        cond = expression(P, true);
    }
    consume0(P, tokRParen);
    AstNode *body = comptimeBlock(P, parser);

    AstNode *otherwise = NULL;
    if (match(P, tokElse)) {
        if (check(P, tokLBrace)) {
            otherwise = comptimeBlock(P, parser);
        }
        else {
            consume0(P, tokHash);
            otherwise = parseComptimeIf(P, parser);
        }
    }

    return makeAstNode(
        P->memPool,
        &tok.fileLoc,
        &(AstNode){
            .tag = astIfStmt,
            .flags = flgComptime,
            .ifStmt = {.cond = cond, .body = body, .otherwise = otherwise}});
}

static AstNode *parseComptimeWhile(Parser *P, AstNode *(*parser)(Parser *))
{
    AstNode *cond;
    Token tok = *consume0(P, tokWhile);
    consume0(P, tokLParen);
    if (check(P, tokConst, tokVar)) {
        cond = variable(P, false, false, true, false);
    }
    else {
        cond = expression(P, true);
    }
    consume0(P, tokRParen);

    AstNode *body = comptimeBlock(P, parser);

    return makeAstNode(P->memPool,
                       &tok.fileLoc,
                       &(AstNode){.tag = astWhileStmt,
                                  .flags = flgComptime,
                                  .whileStmt = {.cond = cond, .body = body}});
}

static AstNode *parseComptimeFor(Parser *P, AstNode *(*parser)(Parser *))
{
    Token tok = *consume0(P, tokFor);
    consume0(P, tokLParen);
    AstNode *var = variable(P, false, false, true, true);
    consume0(P, tokColon);
    AstNode *range = expression(P, true);
    consume0(P, tokRParen);

    AstNode *body = comptimeBlock(P, parser);

    return makeAstNode(
        P->memPool,
        &tok.fileLoc,
        &(AstNode){.tag = astForStmt,
                   .flags = flgComptime,
                   .forStmt = {.var = var, .range = range, .body = body}});
}

static AstNode *parseComptimeVarDecl(Parser *P, AstNode *(*parser)(Parser *))
{
    AstNode *node = variable(P, false, false, true, false);
    node->flags |= flgComptime;
    return node;
}

static AstNode *comptime(Parser *P, AstNode *(*parser)(Parser *))
{
    if (!match(P, tokHash)) {
        return parser(P);
    }

    switch (current(P)->tag) {
    case tokIf:
        return parseComptimeIf(P, parser);
    case tokWhile:
        return parseComptimeWhile(P, parser);
    case tokFor:
        return parseComptimeFor(P, parser);
    case tokConst:
        return parseComptimeVarDecl(P, parser);
    default:
        parserError(P,
                    &current(P)->fileLoc,
                    "current token is not a valid comptime statement",
                    NULL);
    }
    unreachable("");
}

static AstNode *annotation(Parser *P)
{
    Token tok = *consume0(P, tokQuote);
    Token name = *consume0(P, tokIdent);
    consume0(P, tokAssign);
    AstNode *value = expression(P, false);
    match(P, tokSemicolon);
    return newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astAnnotation,
                   .annotation = {.name = getTokenString(P, &name, false),
                                  .value = value}});
}

static inline AstNode *annotations(Parser *P)
{
    AstNodeList list = {NULL};
    while (check(P, tokQuote)) {
        insertAstNode(&list, annotation(P));
    }
    return list.first;
}

static AstNode *classOrStructDecl(Parser *P, bool isPublic, bool isExtern)
{
    AstNode *base = NULL, *gParams = NULL, *implements = NULL, *annots = NULL;
    AstNodeList members = {NULL};
    Token tok = *match(P, tokClass, tokStruct);
    cstring name = getTokenString(P, consume0(P, tokIdent), false);

    if (!isExtern) {
        if (match(P, tokLBracket)) {
            gParams = parseGenericParams(P);
            consume0(P, tokRBracket);
        }

        if (match(P, tokColon)) {
            // Only classes support inheritance
            if (tok.tag == tokStruct || !check(P, tokColon))
                base = parseType(P);
            if (tok.tag == tokClass && match(P, tokColon))
                implements = parseAtLeastOne(P,
                                             "interface to implement",
                                             tokLBrace,
                                             tokComma,
                                             parseType);
        }
    }
    else {
        implements = NULL;
    }

    if (!isExtern || check(P, tokLBrace)) {
        consume0(P, tokLBrace);
        if (check(P, tokQuote))
            annots = annotations(P);
        while (!check(P, tokRBrace, tokEoF)) {
            listAddAstNode(&members, comptime(P, comptimeClassOrStructDecl));
        }
        consume0(P, tokRBrace);
    }

    AstNode *node = newAstNode(
        P,
        &tok,
        &(AstNode){.tag = tok.tag == tokClass ? astClassDecl : astStructDecl,
                   .flags = ((isPublic ? flgPublic : flgNone) |
                             (isExtern ? flgExtern : flgNone)),
                   .classDecl = {.name = name,
                                 .members = members.first,
                                 .implements = implements,
                                 .base = base,
                                 .annotations = annots}});

    if (gParams) {
        return newAstNode(
            P,
            &tok,
            &(AstNode){.tag = astGenericDecl,
                       .flags = isPublic ? flgPublic : flgNone,
                       .genericDecl = {.params = gParams, .decl = node}});
    }
    return node;
}

static AstNode *interfaceDecl(Parser *P, bool isPublic)
{
    AstNode *gParams = NULL;
    AstNodeList members = {NULL};
    Token tok = *consume0(P, tokInterface);
    cstring name = getTokenString(P, consume0(P, tokIdent), false);

    if (match(P, tokLBracket)) {
        gParams = parseGenericParams(P);
        consume0(P, tokRBracket);
    }

    consume0(P, tokLBrace);
    while (!check(P, tokRBrace, tokEoF)) {
        listAddAstNode(&members, comptime(P, parseInterfaceMember));
    }
    consume0(P, tokRBrace);

    AstNode *node = newAstNode(
        P,
        &tok,
        &(AstNode){.tag = astInterfaceDecl,
                   .flags = isPublic ? flgPublic : flgNone,
                   .interfaceDecl = {.name = name, .members = members.first}});

    if (gParams) {
        return newAstNode(
            P,
            &tok,
            &(AstNode){.tag = astGenericDecl,
                       .flags = isPublic ? flgPublic : flgNone,
                       .genericDecl = {.params = gParams, .decl = node}});
    }
    return node;
}

static AstNode *enumOption(Parser *P)
{
    AstNode *value = NULL;
    Token tok = *current(P);
    AstNode *attrs = NULL;
    if (check(P, tokAt))
        attrs = attributes(P);

    cstring name = getTokenString(P, consume0(P, tokIdent), false);
    if (match(P, tokAssign)) {
        value = expression(P, false);
    }
    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = astEnumOptionDecl,
                                 .attrs = attrs,
                                 .enumOption = {.name = name, .value = value}});
}

static AstNode *enumDecl(Parser *P, bool isPublic)
{
    AstNode *base = NULL, *options = NULL;
    Token tok = *consume0(P, tokEnum);
    cstring name = getTokenString(P, consume0(P, tokIdent), false);

    if (match(P, tokColon)) {
        base = parseType(P);
    }
    consume0(P, tokLBrace);
    options =
        parseAtLeastOne(P, "enum options", tokRBrace, tokComma, enumOption);
    consume0(P, tokRBrace);

    return newAstNode(
        P,
        &tok,
        &(AstNode){
            .tag = astEnumDecl,
            .flags = isPublic ? flgPublic : flgNone,
            .enumDecl = {.options = options, .name = name, .base = base}});
}

static AstNode *aliasDecl(Parser *P, bool isPublic, bool isExtern)
{
    AstNode *alias = NULL;
    Token tok = *consume0(P, tokType);
    u64 flags = isPublic ? flgPublic : flgNone;
    flags |= isExtern ? flgExtern : flgNone;
    cstring name = getTokenString(P, consume0(P, tokIdent), false);
    if (!isExtern) {
        if (match(P, tokAssign))
            alias = parseType(P);
        else {
            flags |= flgForwardDecl;
            alias = NULL;
        }
    }
    match(P, tokSemicolon);

    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = astTypeDecl,
                                 .flags = flags,
                                 .typeDecl = {.name = name, .aliased = alias}});
}

static AstNode *parseCCode(Parser *P)
{
    Token tok = *previous(P);
    if (!match(P, tokCDefine, tokCInclude, tokCSources)) {
        parserError(P,
                    &tok.fileLoc,
                    "unexpected attribute, expecting either `@cDefine` or "
                    "`@cInclude`",
                    NULL);
    }
    CCodeKind kind = getCCodeKind(previous(P)->tag);
    consume0(P, tokLParen);
    AstNode *code = NULL;
    if (kind == cSources) {
        code = parseMany(P, tokRParen, tokComma, parseString);
    }
    else {
        code = parseString(P);
    }
    consume0(P, tokRParen);

    return makeAstNode(
        P->memPool,
        &tok.fileLoc,
        &(AstNode){.tag = astCCode, .cCode = {.what = code, .kind = kind}});
}

static AstNode *declaration(Parser *P)
{
    Token tok = *current(P);
    AstNode *attrs = NULL, *decl = NULL;
    if (check(P, tokAt) && peek(P, 1)->tag == tokCDefine) {
        advance(P);
        return parseCCode(P);
    }

    if (check(P, tokAt))
        attrs = attributes(P);
    bool isPublic = match(P, tokPub) != NULL;
    bool isExtern = false;
    if (check(P, tokExtern)) {
        // do we need to consume native
        switch (peek(P, 1)->tag) {
        case tokType:
        case tokVar:
        case tokConst:
        case tokFunc:
        case tokStruct:
            advance(P);
            isExtern = true;
            break;
        case tokAsync:
        case tokMacro:
        case tokClass:
        case tokTest:
        case tokException:
            parserError(P,
                        &current(P)->fileLoc,
                        "declaration cannot be marked as extern",
                        NULL);
        default:
            break;
        }
    }

    if (isPublic) {
        // is pub valid in this context
        switch (peek(P, 1)->tag) {
        case tokMacro:
        case tokTest:
            parserError(P,
                        &current(P)->fileLoc,
                        "declaration cannot be marked as public",
                        NULL);
        default:
            break;
        }
    }

    switch (current(P)->tag) {
    case tokStruct:
    case tokClass:
        decl = classOrStructDecl(P, isPublic, isExtern);
        break;
    case tokInterface:
        decl = interfaceDecl(P, isPublic);
        break;
    case tokEnum:
        decl = enumDecl(P, isPublic);
        break;
    case tokType:
        decl = aliasDecl(P, isPublic, isExtern);
        break;
    case tokVar:
    case tokConst:
        decl = variable(P, isPublic, isExtern, false, false);
        break;
    case tokFunc:
    case tokAsync:
        decl = funcDecl(P,
                        (isPublic ? flgPublic : flgNone) |
                            (isExtern ? flgExtern : flgNone));
        break;
    case tokDefine:
        decl = define(P);
        break;
    case tokMacro:
        if (attrs)
            parserError(P,
                        &tok.fileLoc,
                        "attributes cannot be attached to macro declarations",
                        NULL);
        decl = macroDecl(P, isPublic);
        break;
    case tokTest:
        decl = testDecl(P);
        break;
    case tokException:
        decl = exceptionDecl(P, isPublic ? flgPublic : flgNone);
        break;
    case tokExtern:
        parserError(P,
                    &current(P)->fileLoc,
                    "extern can only be used on top level struct, function or "
                    "variable declarations",
                    NULL);
        break;
    default:
        reportUnexpectedToken(P, "a declaration");
    }

#undef isExtern
    if (decl) {
        decl->flags |= flgTopLevelDecl;
        decl->attrs = attrs;
    }
    return decl;
}

static AstNode *comptimeDeclaration(Parser *P)
{
    return comptime(P, declaration);
}

static void synchronize(Parser *P)
{
    // skip current problematic token
    advance(P);
    while (!match(P, tokSemicolon, tokEoF)) {
        switch (current(P)->tag) {
        case tokType:
        case tokStruct:
        case tokClass:
        case tokEnum:
        case tokVar:
        case tokConst:
        case tokAsync:
        case tokFunc:
        case tokAt:
        case tokDefine:
        case tokCDefine:
        case tokInterface:
        case tokPub:
        case tokMacro:
        case tokIf:
        case tokFor:
        case tokSwitch:
        case tokWhile:
        case tokMatch:
            return;
        default:
            advance(P);
        }
    }
}

static AstNode *parseImportEntity(Parser *P)
{
    Token tok = *consume0(P, tokIdent);
    cstring name = getTokenString(P, &tok, false), alias;
    if (match(P, tokAs)) {
        Token *aliasTok = consume0(P, tokIdent);
        alias = getTokenString(P, aliasTok, false);
    }
    else {
        alias = name;
    }

    return makeAstNode(
        P->memPool,
        &tok.fileLoc,
        &(AstNode){.tag = astImportEntity,
                   .importEntity = {.alias = alias, .name = name}});
}

static AstNode *parseModuleDecl(Parser *P)
{
    Token tok = *consume0(P, tokModule);
    Token name = *consume0(P, tokIdent);

    return makeAstNode(
        P->memPool,
        &tok.fileLoc,
        &(AstNode){.tag = astModuleDecl,
                   .moduleDecl = {.name = getTokenString(P, &name, false)}});
}

static void skipImportTestDecl(Parser *P)
{
    bool hasEntities = false;
    if (match(P, tokLBrace)) {
        hasEntities = true;
        while (!match(P, tokEoF, tokRBrace))
            advance(P);
        consume0(P, tokFrom);
    }
    else if (match(P, tokIdent)) {
        hasEntities = true;
        consume0(P, tokFrom);
    }
    consume0(P, tokStringLiteral);
    if (hasEntities && match(P, tokAs))
        consume0(P, tokIdent);
}

static AstNode *parseImportDecl(Parser *P)
{
    Token tok = *consume0(P, tokImport);
    if (match(P, tokPlugin)) {
        cstring path = getStringLiteral(P, consume0(P, tokStringLiteral));
        consume0(P, tokAs);
        cstring name = getTokenString(P, consume0(P, tokIdent), false);
        FileLoc loc = locSubRange(&tok.fileLoc, &previous(P)->fileLoc);
        Plugin *plugin = loadPlugin(P->cc, &loc, name, path);
        if (plugin == NULL) {
            parserError(P,
                        &tok.fileLoc,
                        "loading plugin {s} from {s} failed",
                        (FormatArg[]){{.s = name}, {.s = path}});
        }
        return makeAstNode(
            P->memPool,
            &tok.fileLoc,
            &(AstNode){.tag = astPluginDecl,
                       .pluginDecl = {.plugin = plugin, .name = name}});
    }
    bool testMode = match(P, tokTest);
    if (testMode && !P->testMode) {
        skipImportTestDecl(P);
        return NULL;
    }

    AstNode *module;
    AstNode *entities = NULL, *alias = NULL;
    const Type *exports;
    if (check(P, tokIdent)) {
        entities = parseImportEntity(P);
    }
    else if (match(P, tokLBrace)) {
        entities = parseAtLeastOne(
            P, "exported declaration", tokRBrace, tokComma, parseImportEntity);
        consume0(P, tokRBrace);
    }

    if (entities)
        consume0(P, tokFrom);

    module = parseString(P);

    if (entities == NULL && match(P, tokAs))
        alias = parseIdentifier(P);

    exports = compileModule(P->cc, module, entities, alias, testMode);
    if (exports == NULL) {
        parserFail(P,
                   &tok.fileLoc,
                   "importing module {s} failed",
                   (FormatArg[]){{.s = module->stringLiteral.value}});
    }

    return makeAstNode(P->memPool,
                       &tok.fileLoc,
                       &(AstNode){.tag = astImportDecl,
                                  .type = exports,
                                  .flags = exports->flags,
                                  .import = {.module = module,
                                             .alias = alias,
                                             .entities = entities}});
}

static AstNode *parseImportsDecl(Parser *P)
{
    AstNodeList imports = {};
    while (check(P, tokImport)) {
        AstNode *import = parseImportDecl(P);
        if (import)
            insertAstNode(&imports, import);
    }

    return imports.first;
}

static AstNode *parseTopLevelDecl(Parser *P)
{
    if (check(P, tokImport))
        return parseImportDecl(P);
    else if (check(P, tokCDefine, tokCInclude, tokCSources)) {
        return parseCCode(P);
    }

    Token tok = *consume0(P, tokCBuild);
    cstring choice = NULL;
    if (match(P, tokColon)) {
        Token choiceToken = *consume0(P, tokIdent);
        choice = getTokenString(P, &choiceToken, false);
    }
    Token itemToken = *consume0(P, tokStringLiteral);
    cstring item = getTokenString(P, &itemToken, true);
    if (choice == NULL || choice == S_src)
        addNativeSourceFile(P->cc, tok.fileLoc.fileName, item);
    else if (choice == S_clib) {
        addLinkLibrary(P->cc, item);
    }
    else {
        parserFail(
            P,
            &tok.fileLoc,
            "Unsupported c-code token '{s}', user either `:clib` or `:src`",
            (FormatArg[]){{.s = choice}});
    }
    return NULL;
}

static AstNode *conditionalTopLevelDecl(Parser *P)
{
    Token tok = *current(P);
    if (match(P, tokDefine)) {
        consume0(P, tokIf);
        consume0(P, tokLParen);
        AstNode *cond = expression(P, false);
        consume0(P, tokRParen);
        cond = preprocessAst(P->cc, cond);
        if (!nodeIs(cond, BoolLit)) {
            parserError(P,
                        &tok.fileLoc,
                        "invalid condition, condition must evaluate to a "
                        "boolean literal",
                        NULL);
        }

        if (!cond->boolLiteral.value) {
            // skip compilation block
            consume0(P, tokLBrace);
            skipUntil(P, tokRBrace);
            consume0(P, tokRBrace);
            if (match(P, tokElse))
                return conditionalTopLevelDecl(P);
            else
                return NULL;
        }
        else {
            consume0(P, tokLBrace);
            AstNode *nodes =
                parseManyNoSeparator(P, tokRBrace, parseTopLevelDecl);
            consume0(P, tokRBrace);
            while (match(P, tokElse)) {
                if (match(P, tokDefine))
                    skipUntil(P, tokLBrace);
                consume0(P, tokLBrace);
                skipUntil(P, tokRBrace);
                consume0(P, tokRBrace);
            }
            return nodes;
        }
    }
    else if (match(P, tokLBrace)) {
        AstNode *nodes = parseManyNoSeparator(P, tokRBrace, parseTopLevelDecl);
        consume0(P, tokRBrace);
        return nodes;
    }
    else {
        return parseTopLevelDecl(P);
    }
}

static void synchronizeUntil(Parser *P, TokenTag tag)
{
    while (!check(P, tag, tokEoF))
        advance(P);
}

Parser makeParser(Lexer *lexer, CompilerDriver *cc, bool testMode)
{
    Parser parser = {.cc = cc,
                     .lexer = lexer,
                     .L = lexer->log,
                     .memPool = cc->pool,
                     .strPool = cc->strings,
                     .testMode = testMode};
    parser.ahead[0] = (Token){.tag = tokEoF};
    for (u32 i = 1; i < TOKEN_BUFFER; i++)
        parser.ahead[i] = advanceLexer_(&parser);

    return parser;
}

AstNode *parseProgram(Parser *P)
{
    Token tok = *current(P);

    AstNodeList decls = {NULL};
    AstNode *module = NULL;
    AstNodeList topLevel = {NULL};

    if (check(P, tokModule))
        module = parseModuleDecl(P);

    while (check(P, tokImport) || check(P, tokDefine) ||
           (check(P, tokAt) &&
            checkPeek(P, 1, tokCDefine, tokCInclude, tokCSources, tokCBuild) &&
            match(P, tokAt))) {
        E4C_TRY_BLOCK(
            {
                AstNode *node = NULL;
                node = conditionalTopLevelDecl(P);
                if (node != NULL)
                    listAddAstNode(&topLevel, node);
            } E4C_CATCH(ParserException) {
                synchronize(E4C_EXCEPTION.ctx);
            } E4C_CATCH(ParserAbort) { return NULL; })
    }

    while (!isEoF(P)) {
        E4C_TRY_BLOCK(
            {
                AstNode *decl = comptime(P, comptimeDeclaration);
                if (decl)
                    listAddAstNode(&decls, decl);
            } E4C_CATCH(ParserException) { synchronize(E4C_EXCEPTION.ctx); })
    }

    return newAstNode(P,
                      &tok,
                      &(AstNode){.tag = astProgram,
                                 .program = {.module = module,
                                             .top = topLevel.first,
                                             .decls = decls.first}});
}

AstNode *parseExpression(Parser *P) { return expression(P, false); }
