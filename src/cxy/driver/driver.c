#include "driver/driver.h"
#include "core/log.h"
#include "core/mempool.h"
#include "core/utils.h"
#include "driver/options.h"
#include "lang/ast.h"
#include "lang/codegen.h"
#include "lang/lexer.h"
#include "lang/parser.h"
#include "lang/tcheck.h"
#include "lang/ttable.h"

static AstNode *parseFile(const char *fileName, MemPool *memPool, Log *log)
{
    size_t file_size = 0;
    char *fileData = readFile(fileName, &file_size);
    if (!fileData) {
        logError(log,
                 NULL,
                 "cannot open file '{s}'",
                 (FormatArg[]){{.s = fileName}});
        return NULL;
    }

    Lexer lexer = newLexer(fileName, fileData, file_size, log);
    Parser parser = makeParser(&lexer, memPool);
    AstNode *program = parseProgram(&parser);

    freeLexer(&lexer);
    free(fileData);

    return program;
}

bool compileFile(const char *fileName, const Options *options, Log *log)
{
    MemPool memPool = newMemPool();
    AstNode *program = parseFile(fileName, &memPool, log);

    if (!options->noTypeCheck && log->errorCount == 0) {
        TypeTable *table = newTypeTable(&memPool);
        typeCheck(program, log, &memPool, table);
        freeTypeTable(table);
    }

    if (log->errorCount == 0) {
        TypeTable *table = newTypeTable(&memPool);
        generateCode(table, program);
        freeTypeTable(table);
    }

    if (options->printAst && log->errorCount == 0) {
        FormatState state = newFormatState(
            "    ", log->state->ignoreStyle || !isColorSupported(stdout));
        printAst(&state, program);
        writeFormatState(&state, stdout);
        freeFormatState(&state);
        printf("\n");
    }

    return program;
}
