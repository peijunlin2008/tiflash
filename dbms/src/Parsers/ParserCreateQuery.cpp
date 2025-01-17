// Copyright 2023 PingCAP, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <Parsers/ASTCreateQuery.h>
#include <Parsers/ASTExpressionList.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ExpressionListParsers.h>
#include <Parsers/ParserCreateQuery.h>
#include <Parsers/ParserSelectWithUnionQuery.h>
#include <Parsers/ParserSetQuery.h>


namespace DB
{
bool ParserNestedTable::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    ParserToken open(TokenType::OpeningRoundBracket);
    ParserToken close(TokenType::ClosingRoundBracket);
    ParserIdentifier name_p;
    ParserNameTypePairList columns_p;

    ASTPtr name;
    ASTPtr columns;

    /// For now `name == 'Nested'`, probably alternative nested data structures will appear
    if (!name_p.parse(pos, name, expected))
        return false;

    if (!open.ignore(pos))
        return false;

    if (!columns_p.parse(pos, columns, expected))
        return false;

    if (!close.ignore(pos))
        return false;

    auto func = std::make_shared<ASTFunction>();
    func->name = typeid_cast<ASTIdentifier &>(*name).name;
    func->arguments = columns;
    func->children.push_back(columns);
    node = func;

    return true;
}


bool ParserIdentifierWithParameters::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    ParserFunction function_or_array;
    if (function_or_array.parse(pos, node, expected))
        return true;

    ParserNestedTable nested;
    return nested.parse(pos, node, expected);
}


bool ParserIdentifierWithOptionalParameters::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    ParserIdentifier non_parametric;
    ParserIdentifierWithParameters parametric;

    if (parametric.parse(pos, node, expected))
        return true;

    ASTPtr ident;
    if (non_parametric.parse(pos, ident, expected))
    {
        auto func = std::make_shared<ASTFunction>();
        func->name = typeid_cast<ASTIdentifier &>(*ident).name;
        node = func;
        return true;
    }

    return false;
}

bool ParserTypeInCastExpression::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    if (ParserIdentifierWithOptionalParameters().parse(pos, node, expected))
    {
        const auto & id_with_params = typeid_cast<const ASTFunction &>(*node);
        node = std::make_shared<ASTIdentifier>(String{id_with_params.range.first, id_with_params.range.second});
        return true;
    }

    return false;
}

bool ParserNameTypePairList::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    return ParserList(std::make_unique<ParserNameTypePair>(), std::make_unique<ParserToken>(TokenType::Comma), false)
        .parse(pos, node, expected);
}

bool ParserColumnDeclarationList::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    return ParserList(
               std::make_unique<ParserColumnDeclaration>(),
               std::make_unique<ParserToken>(TokenType::Comma),
               false)
        .parse(pos, node, expected);
}


bool ParserStorage::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    ParserKeyword s_engine("ENGINE");
    ParserToken s_eq(TokenType::Equals);
    ParserKeyword s_partition_by("PARTITION BY");
    ParserKeyword s_order_by("ORDER BY");
    ParserKeyword s_sample_by("SAMPLE BY");
    ParserKeyword s_settings("SETTINGS");

    ParserIdentifierWithOptionalParameters ident_with_optional_params_p;
    ParserExpression expression_p;
    ParserSetQuery settings_p(/* parse_only_internals_ = */ true);

    ASTPtr engine;
    ASTPtr partition_by;
    ASTPtr order_by;
    ASTPtr sample_by;
    ASTPtr settings;

    if (!s_engine.ignore(pos, expected))
        return false;

    s_eq.ignore(pos, expected);

    if (!ident_with_optional_params_p.parse(pos, engine, expected))
        return false;

    while (true)
    {
        if (!partition_by && s_partition_by.ignore(pos, expected))
        {
            if (expression_p.parse(pos, partition_by, expected))
                continue;
            else
                return false;
        }

        if (!order_by && s_order_by.ignore(pos, expected))
        {
            if (expression_p.parse(pos, order_by, expected))
                continue;
            else
                return false;
        }

        if (!sample_by && s_sample_by.ignore(pos, expected))
        {
            if (expression_p.parse(pos, sample_by, expected))
                continue;
            else
                return false;
        }

        if (s_settings.ignore(pos, expected))
        {
            if (!settings_p.parse(pos, settings, expected))
                return false;
        }

        break;
    }

    auto storage = std::make_shared<ASTStorage>();
    storage->set(storage->engine, engine);
    storage->set(storage->partition_by, partition_by);
    storage->set(storage->order_by, order_by);
    storage->set(storage->sample_by, sample_by);
    storage->set(storage->settings, settings);

    node = storage;
    return true;
}


bool ParserCreateQuery::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    ParserKeyword s_create("CREATE");
    ParserKeyword s_attach("ATTACH");
    ParserKeyword s_table("TABLE");
    ParserKeyword s_database("DATABASE");
    ParserKeyword s_if_not_exists("IF NOT EXISTS");
    ParserToken s_dot(TokenType::Dot);
    ParserToken s_lparen(TokenType::OpeningRoundBracket);
    ParserToken s_rparen(TokenType::ClosingRoundBracket);
    ParserStorage storage_p;
    ParserIdentifier name_p;
    ParserColumnDeclarationList columns_p;
    ParserSelectWithUnionQuery select_p;

    ASTPtr database;
    ASTPtr table;
    ASTPtr columns;
    ASTPtr storage;
    bool attach = false;
    bool if_not_exists = false;

    if (!s_create.ignore(pos, expected))
    {
        if (s_attach.ignore(pos, expected))
            attach = true;
        else
            return false;
    }

    if (s_table.ignore(pos, expected))
    {
        if (s_if_not_exists.ignore(pos, expected))
            if_not_exists = true;

        if (!name_p.parse(pos, table, expected))
            return false;

        if (s_dot.ignore(pos, expected))
        {
            database = table;
            if (!name_p.parse(pos, table, expected))
                return false;
        }

        // Shortcut for ATTACH a previously detached table
        if (attach && (!pos.isValid() || pos.get().type == TokenType::Semicolon))
        {
            auto query = std::make_shared<ASTCreateQuery>();
            node = query;

            query->attach = attach;
            query->if_not_exists = if_not_exists;

            if (database)
                query->database = typeid_cast<ASTIdentifier &>(*database).name;
            if (table)
                query->table = typeid_cast<ASTIdentifier &>(*table).name;

            return true;
        }

        /// List of columns.
        if (s_lparen.ignore(pos, expected))
        {
            if (!columns_p.parse(pos, columns, expected))
                return false;

            if (!s_rparen.ignore(pos, expected))
                return false;

            if (!storage_p.parse(pos, storage, expected))
                return false;
        }
        else
        {
            return false;
        }
    }
    else if (s_database.ignore(pos, expected))
    {
        if (s_if_not_exists.ignore(pos, expected))
            if_not_exists = true;

        if (!name_p.parse(pos, database, expected))
            return false;

        storage_p.parse(pos, storage, expected);
    }
    else
    {
        return false;
    }

    auto query = std::make_shared<ASTCreateQuery>();
    node = query;

    query->attach = attach;
    query->if_not_exists = if_not_exists;

    if (database)
        query->database = typeid_cast<ASTIdentifier &>(*database).name;
    if (table)
        query->table = typeid_cast<ASTIdentifier &>(*table).name;

    query->set(query->columns, columns);
    query->set(query->storage, storage);

    return true;
}


} // namespace DB
