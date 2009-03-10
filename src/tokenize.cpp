/*
 * Cppcheck - A tool for static C/C++ code analysis
 * Copyright (C) 2007-2009 Daniel Marjamäki, Reijo Tomperi, Nicolas Le Cam,
 * Leandro Penz, Kimmo Varis, Vesa Pikki
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/
 */


//---------------------------------------------------------------------------
#include "tokenize.h"
#include "filelister.h"

#include <locale>
#include <fstream>
#include <string>
#include <cstring>
#include <iostream>
#include <sstream>
#include <list>
#include <algorithm>
#include <cstdlib>
#include <cctype>

//---------------------------------------------------------------------------

Tokenizer::Tokenizer()
{
    _tokens = 0;
    _tokensBack = 0;
}

Tokenizer::~Tokenizer()
{
    DeallocateTokens();
}

//---------------------------------------------------------------------------

// Helper functions..


//---------------------------------------------------------------------------

const Token *Tokenizer::tokens() const
{
    return _tokens;
}


const std::vector<std::string> *Tokenizer::getFiles() const
{
    return &_files;
}

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// addtoken
// add a token. Used by 'Tokenizer'
//---------------------------------------------------------------------------

void Tokenizer::addtoken(const char str[], const unsigned int lineno, const unsigned int fileno)
{
    if (str[0] == 0)
        return;

    // Replace hexadecimal value with decimal
    std::ostringstream str2;
    if (strncmp(str, "0x", 2) == 0)
    {
        str2 << std::strtoul(str + 2, NULL, 16);
    }
    else
    {
        str2 << str;
    }

    if (_tokensBack)
    {
        _tokensBack->insertToken(str2.str().c_str());
        _tokensBack = _tokensBack->next();
    }
    else
    {
        _tokens = new Token;
        _tokensBack = _tokens;
        _tokensBack->str(str2.str().c_str());
    }

    _tokensBack->linenr(lineno);
    _tokensBack->fileIndex(fileno);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// SizeOfType - gives the size of a type
//---------------------------------------------------------------------------



int Tokenizer::SizeOfType(const char type[]) const
{
    if (!type)
        return 0;

    std::map<std::string, unsigned int>::const_iterator it = _typeSize.find(type);
    if (it == _typeSize.end())
        return 0;

    return it->second;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// InsertTokens - Copy and insert tokens
//---------------------------------------------------------------------------

void Tokenizer::InsertTokens(Token *dest, Token *src, unsigned int n)
{
    while (n > 0)
    {
        dest->insertToken(src->aaaa());
        dest = dest->next();
        dest->fileIndex(src->fileIndex());
        dest->linenr(src->linenr());
        dest->varId(src->varId());
        src  = src->next();
        --n;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Tokenize - tokenizes a given file.
//---------------------------------------------------------------------------

void Tokenizer::tokenize(std::istream &code, const char FileName[])
{
    // The "_files" vector remembers what files have been tokenized..
    _files.push_back(FileLister::simplifyPath(FileName));

    // line number in parsed code
    unsigned int lineno = 1;

    // The current token being parsed
    std::string CurrentToken;

    // lineNumbers holds line numbers for files in fileIndexes
    // every time an include file is complitely parsed, last item in the vector
    // is removed and lineno is set to point to that value.
    std::vector<unsigned int> lineNumbers;

    // fileIndexes holds index for _files vector about currently parsed files
    // every time an include file is complitely parsed, last item in the vector
    // is removed and FileIndex is set to point to that value.
    std::vector<unsigned int> fileIndexes;

    // FileIndex. What file in the _files vector is read now?
    unsigned int FileIndex = 0;

    // Read one byte at a time from code and create tokens
    for (char ch = (char)code.get(); code.good(); ch = (char)code.get())
    {
        // We are not handling UTF and stuff like that. Code is supposed to plain simple text.
        if (ch < 0)
            continue;

        if (ch == '\n')
        {
            // Add current token..
            addtoken(CurrentToken.c_str(), lineno++, FileIndex);
            CurrentToken.clear();
            continue;
        }

        // char/string..
        if (ch == '\'' || ch == '\"')
        {
            // Add previous token
            addtoken(CurrentToken.c_str(), lineno, FileIndex);
            CurrentToken.clear();

            // read char
            bool special = false;
            char c = ch;
            do
            {
                // Append token..
                CurrentToken += c;

                if (c == '\n')
                    ++lineno;

                // Special sequence '\.'
                if (special)
                    special = false;
                else
                    special = (c == '\\');

                // Get next character
                c = (char)code.get();
            }
            while (code.good() && (special || c != ch));
            CurrentToken += ch;

            // Add token and start on next..
            addtoken(CurrentToken.c_str(), lineno, FileIndex);
            CurrentToken.clear();

            continue;
        }

        if (ch == '#' && CurrentToken.empty())
        {
            // If previous token was "#" then append this to create a "##" token
            if (Token::simpleMatch(_tokensBack, "#"))
            {
                _tokensBack->str("##");
                continue;
            }

            std::string line("#");
            {
                char chPrev = '#';
                bool skip = false;
                while (code.good())
                {
                    ch = (char)code.get();
                    if (chPrev != '\\' && ch == '\n')
                        break;
                    if (chPrev == '\\')
                        line += chPrev;
                    if (chPrev == '#' && ch == '#')
                    {
                        addtoken("##", lineno, FileIndex);
                        skip = true;
                        break;
                    }
                    if (ch != ' ')
                        chPrev = ch;
                    if (ch != '\\' && ch != '\n')
                    {
                        line += ch;
                    }
                    if (ch == '\n')
                        ++lineno;
                }
                if (skip)
                    continue;
            }
            if (strncmp(line.c_str(), "#file", 5) == 0 &&
                line.find("\"") != std::string::npos)
            {
                // Extract the filename
                line.erase(0, line.find("\"") + 1);
                if (line.find("\"") != std::string::npos)
                    line.erase(line.find("\""));

                // Has this file been tokenized already?
                ++lineno;
                bool foundOurfile = false;
                fileIndexes.push_back(FileIndex);
                for (unsigned int i = 0; i < _files.size(); i++)
                {
                    if (FileLister::SameFileName(_files[i].c_str(), line.c_str()))
                    {
                        // Use this index
                        foundOurfile = true;
                        FileIndex = i;
                    }
                }

                if (!foundOurfile)
                {
                    // The "_files" vector remembers what files have been tokenized..
                    _files.push_back(FileLister::simplifyPath(line.c_str()));
                    FileIndex = static_cast<unsigned int>(_files.size() - 1);
                }

                lineNumbers.push_back(lineno);
                lineno = 1;

                continue;
            }

            else if (strncmp(line.c_str(), "#endfile", 8) == 0)
            {
                if (lineNumbers.empty() || fileIndexes.empty())
                {
                    std::cerr << "####### Preprocessor bug! #######\n";
                    std::exit(0);
                }

                lineno = lineNumbers.back();
                lineNumbers.pop_back();
                FileIndex = fileIndexes.back();
                fileIndexes.pop_back();
                continue;
            }

            else
            {
                addtoken(line.c_str(), lineno, FileIndex);
            }
        }

        if (strchr("#+-*/%&|^?!=<>[](){};:,.~", ch))
        {
            if (ch == '.' &&
                CurrentToken.length() > 0 &&
                std::isdigit(CurrentToken[0]))
            {
                // Don't separate doubles "5.4"
            }
            else if (strchr("+-", ch) &&
                     CurrentToken.length() > 0 &&
                     std::isdigit(CurrentToken[0]) &&
                     CurrentToken[CurrentToken.length()-1] == 'e')
            {
                // Don't separate doubles "4.2e+10"
            }
            else
            {
                addtoken(CurrentToken.c_str(), lineno, FileIndex);
                CurrentToken.clear();
                CurrentToken += ch;
                // Add "++", "--" or ">>" token
                if ((ch == '+' || ch == '-' || ch == '>') && (code.peek() == ch))
                    CurrentToken += (char)code.get();
                addtoken(CurrentToken.c_str(), lineno, FileIndex);
                CurrentToken.clear();
                continue;
            }
        }


        if (std::isspace(ch) || std::iscntrl(ch))
        {
            addtoken(CurrentToken.c_str(), lineno, FileIndex);
            CurrentToken.clear();
            continue;
        }

        CurrentToken += ch;
    }
    addtoken(CurrentToken.c_str(), lineno, FileIndex);

    // Combine tokens..
    for (Token *tok = _tokens; tok && tok->next(); tok = tok->next())
    {
        static const char* combineWithNext[][3] =
        {
            { "<", "<", "<<" },

            { "&", "&", "&&" },
            { "|", "|", "||" },

            { "+", "=", "+=" },
            { "-", "=", "-=" },
            { "*", "=", "*=" },
            { "/", "=", "/=" },
            { "&", "=", "&=" },
            { "|", "=", "|=" },

            { "=", "=", "==" },
            { "!", "=", "!=" },
            { "<", "=", "<=" },
            { ">", "=", ">=" },

            { ":", ":", "::" },
            { "-", ">", "." },  // Replace "->" with "."

            { "private", ":", "private:" },
            { "protected", ":", "protected:" },
            { "public", ":", "public:" }
        };

        for (unsigned ui = 0; ui < sizeof(combineWithNext) / sizeof(combineWithNext[0]); ui++)
        {
            if (tok->str() == combineWithNext[ui][0] && tok->next()->str() == combineWithNext[ui][1])
            {
                tok->str(combineWithNext[ui][2]);
                tok->deleteNext();
            }
        }
    }

    // typedef..
    for (Token *tok = _tokens; tok;)
    {
        if (Token::Match(tok, "typedef %type% %type% ;"))
        {
            const char *type1 = tok->strAt(1);
            const char *type2 = tok->strAt(2);
            tok = const_cast<Token*>(tok->tokAt(4));
            for (Token *tok2 = tok; tok2; tok2 = tok2->next())
            {
                if (tok2->str() == type2)
                    tok2->str(type1);
            }
            continue;
        }

        else if (Token::Match(tok, "typedef %type% %type% %type% ;"))
        {
            const char *type1 = tok->strAt(1);
            const char *type2 = tok->strAt(2);
            const char *type3 = tok->strAt(3);
            tok = const_cast<Token*>(tok->tokAt(5));
            for (Token *tok2 = tok; tok2; tok2 = tok2->next())
            {
                if (tok2->str() == type3)
                {
                    tok2->str(type1);
                    tok2->insertToken(type2);
                    tok2 = tok2->next();
                }
            }
            continue;
        }

        tok = tok->next();
    }

    // Remove __asm..
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (Token::simpleMatch(tok->next(), "__asm {"))
        {
            while (tok->next())
            {
                bool last = Token::simpleMatch(tok->next(), "}");

                // Unlink and delete tok->next()
                tok->deleteNext();

                // break if this was the last token to delete..
                if (last)
                    break;
            }
        }
    }

    // Remove "volatile"
    while (Token::simpleMatch(_tokens, "volatile"))
    {
        Token *tok = _tokens;
        _tokens = _tokens->next();
        delete tok;
    }
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        while (Token::simpleMatch(tok->next(), "volatile"))
        {
            tok->deleteNext();
        }
    }

    // Remove "mutable"
    while (Token::simpleMatch(_tokens, "mutable"))
    {
        Token *tok = _tokens;
        _tokens = _tokens->next();
        delete tok;
    }
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        while (Token::simpleMatch(tok->next(), "mutable"))
        {
            tok->deleteNext();
        }
    }

}
//---------------------------------------------------------------------------


void Tokenizer::setVarId()
{
    // Clear all variable ids
    for (Token *tok = _tokens; tok; tok = tok->next())
        tok->varId(0);

    // Set variable ids..
    unsigned int _varId = 0;
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (tok != _tokens && !Token::Match(tok, "[;{}(]"))
            continue;

        if (Token::Match(tok, "[;{}(] %type%"))
            tok = tok->next();

        if (Token::Match(tok, "else|return"))
            continue;

        // Determine name of declared variable..
        const char *varname = 0;
        Token *tok2 = tok->tokAt(1);
        while (tok2 && ! Token::Match(tok2, "[;[=(]"))
        {
            if (tok2->isName())
                varname = tok2->strAt(0);
            else if (tok2->str() != "*")
                break;
            tok2 = tok2->next();
        }

        // Variable declaration found => Set variable ids
        if (Token::Match(tok2, "[;[=]") && varname)
        {
            ++_varId;
            int indentlevel = 0;
            int parlevel = 0;
            bool dot = false;
            for (tok2 = tok->next(); tok2; tok2 = tok2->next())
            {
                if (!dot && tok2->str() == varname)
                    tok2->varId(_varId);
                else if (tok2->str() == "{")
                    ++indentlevel;
                else if (tok2->str() == "}")
                {
                    --indentlevel;
                    if (indentlevel < 0)
                        break;
                }
                else if (tok2->str() == "(")
                    ++parlevel;
                else if (tok2->str() == ")")
                {
                    // Is this a function parameter or a variable declared in for example a for loop?
                    if (parlevel == 0 && indentlevel == 0 && Token::Match(tok2, ") const| {"))
                        ;
                    else
                        --parlevel;
                }
                else if (parlevel < 0 && tok2->str() == ";")
                    break;
                dot = bool(tok2->str() == ".");
            }
        }
    }

    // Struct/Class members
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (tok->varId() != 0 &&
            Token::Match(tok->next(), ". %var%") &&
            tok->tokAt(2)->varId() == 0)
        {
            ++_varId;

            const std::string pattern(std::string(". ") + tok->strAt(2));
            for (Token *tok2 = tok; tok2; tok2 = tok2->next())
            {
                if (tok2->varId() == tok->varId() && Token::simpleMatch(tok2->next(), pattern.c_str()))
                    tok2->next()->next()->varId(_varId);
            }
        }
    }
}


//---------------------------------------------------------------------------
// Simplify token list
//---------------------------------------------------------------------------

void Tokenizer::simplifyTokenList()
{
    // Combine strings
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        while (tok->str()[0] == '"' && tok->next() && tok->next()->str()[0] == '"')
        {
            // Two strings after each other, combine them
            std::string temp = tok->str();
            temp.erase(temp.length() - 1);
            temp.append(tok->next()->str().substr(1));
            tok->str(temp.c_str());
            tok->deleteNext();
        }
    }

    // Remove unwanted keywords
    static const char* unwantedWords[] = { "unsigned", "unlikely" };
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        for (unsigned ui = 0; ui < sizeof(unwantedWords) / sizeof(unwantedWords[0]) && tok->next(); ui++)
        {
            if (tok->next()->str() == unwantedWords[ui])
            {
                tok->deleteNext();
                break;
            }
        }
    }

    // Convert + + into + and + - into -
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        while (tok->next())
        {
            if (tok->str() == "+")
            {
                if (tok->next()->str() == "+")
                {
                    tok->deleteNext();
                    continue;
                }
                else if (tok->next()->str() == "-")
                {
                    tok->str("-");
                    tok->deleteNext();
                    continue;
                }
            }
            else if (tok->str() == "-")
            {
                if (tok->next()->str() == "-")
                {
                    tok->str("+");
                    tok->deleteNext();
                    continue;
                }
                else if (tok->next()->str() == "+")
                {
                    tok->deleteNext();
                    continue;
                }
            }

            break;
        }
    }



    // Fill the map _typeSize..
    _typeSize.clear();
    _typeSize["char"] = sizeof(char);
    _typeSize["short"] = sizeof(short);
    _typeSize["int"] = sizeof(int);
    _typeSize["long"] = sizeof(long);
    _typeSize["float"] = sizeof(float);
    _typeSize["double"] = sizeof(double);
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (Token::Match(tok, "class|struct %var%"))
        {
            _typeSize[tok->strAt(1)] = 100;
        }
    }


    // Replace 'sizeof(var)'..
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (Token::Match(tok, "[;{}] %type% %var% ;") && tok->tokAt(2)->varId() > 0)
        {
            const unsigned int varid = tok->tokAt(2)->varId();

            // Replace 'sizeof(var)' with 'sizeof(type)'
            int indentlevel = 0;
            for (Token *tok2 = tok; tok2; tok2 = tok2->next())
            {
                if (tok2->str() == "{")
                    ++indentlevel;
                else if (tok2->str() == "}")
                {
                    --indentlevel;
                    if (indentlevel < 0)
                        break;
                }
                else if (Token::Match(tok2, "sizeof ( %varid% )", varid))
                {
                    tok2 = tok2->tokAt(2);
                    tok2->str(tok->strAt(1));
                }
            }
        }
    }


    // Replace 'sizeof(type)'..
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (tok->str() != "sizeof")
            continue;

        if (tok->strAt(1) != std::string("("))
        {
            // Add parenthesis around the sizeof
            for (Token *tempToken = tok->next(); tempToken; tempToken = tempToken->next())
            {
                if (Token::Match(tempToken, "%var%"))
                {
                    if (Token::Match(tempToken->next(), "."))
                    {
                        // We are checking a class or struct, search next varname
                        tempToken = tempToken->tokAt(1);
                        continue;
                    }
                    else if (Token::Match(tempToken->next(), "- >"))
                    {
                        // We are checking a class or struct, search next varname
                        tempToken = tempToken->tokAt(2);
                        continue;
                    }
                    else if (Token::Match(tempToken->next(), "++") ||
                             Token::Match(tempToken->next(), "--"))
                    {
                        // We have variable++ or variable--, there should be
                        // nothing after this
                        tempToken = tempToken->tokAt(2);
                    }
                    else if (Token::Match(tempToken->next(), "["))
                    {
                        // TODO: We need to find closing ], then check for
                        // dots and arrows "var[some[0]]->other"

                        // But for now, just bail out
                        break;
                    }

                    // Ok, we should be clean. Add ) after tempToken
                    tok->insertToken("(");
                    tempToken->insertToken(")");
                    break;
                }
            }
        }

        if (Token::Match(tok, "sizeof ( %type% * )"))
        {
            std::ostringstream str;
            // 'sizeof(type *)' has the same size as 'sizeof(char *)'
            str << sizeof(char *);
            tok->str(str.str().c_str());

            for (int i = 0; i < 4; i++)
            {
                tok->deleteNext();
            }
        }

        else if (Token::Match(tok, "sizeof ( %type% )"))
        {
            const char *type = tok->strAt(2);
            int size = SizeOfType(type);
            if (size > 0)
            {
                std::ostringstream str;
                str << size;
                tok->str(str.str().c_str());
                for (int i = 0; i < 3; i++)
                {
                    tok->deleteNext();
                }
            }
        }

        else if (Token::Match(tok, "sizeof ( * %var% )") || Token::Match(tok, "sizeof ( %var% [ %num% ] )"))
        {
            // Some default value..
            int sz = 100;

            unsigned int varid = tok->tokAt((tok->tokAt(2)->str() == "*") ? 3 : 2)->varId();
            if (varid != 0)
            {
                // Try to locate variable declaration..
                const Token *decltok = Token::findmatch(_tokens, "%type% %varid% [", varid);
                if (decltok)
                {
                    sz = SizeOfType(decltok->strAt(0));
                }
            }

            std::ostringstream ostr;
            ostr << sz;
            tok->str(ostr.str().c_str());
            while (tok->next()->str() != ")")
                tok->deleteNext();
            tok->deleteNext();
        }
    }

    // Replace 'sizeof(var)'
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        // type array [ num ] ;
        if (! Token::Match(tok, "%type% %var% [ %num% ] ;"))
            continue;

        int size = SizeOfType(tok->aaaa());
        if (size <= 0)
            continue;

        const unsigned int varid = tok->next()->varId();
        if (varid == 0)
            continue;

        int total_size = size * std::atoi(tok->strAt(3));

        // Replace 'sizeof(var)' with number
        int indentlevel = 0;
        for (Token *tok2 = tok->tokAt(5); tok2; tok2 = tok2->next())
        {
            if (tok2->str() == "{")
            {
                ++indentlevel;
            }

            else if (tok2->str() == "}")
            {
                --indentlevel;
                if (indentlevel < 0)
                    break;
            }

            else if (Token::Match(tok2, "sizeof ( %varid% )", varid))
            {
                std::ostringstream str;
                str << total_size;
                tok2->str(str.str().c_str());
                // Delete the other tokens..
                for (int i = 0; i < 3; i++)
                {
                    tok2->deleteNext();
                }
            }
        }
    }

    // Replace constants..
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (Token::Match(tok, "const %type% %var% = %num% ;"))
        {
            const char *sym = tok->strAt(2);
            const char *num = tok->strAt(4);
            int indent = 1;
            for (Token *tok2 = tok->tokAt(6); tok2; tok2 = tok2->next())
            {
                if (tok2->str() == "{")
                {
                    ++indent;
                }
                else if (tok2->str() == "}")
                {
                    --indent;
                    if (indent == 0)
                        break;
                }

                // Compare constants, but don't touch members of other structures
                else if (tok2->str() == sym &&
                         tok2->previous() &&
                         tok2->previous()->str() != ".")
                {
                    tok2->str(num);
                }
            }
        }
    }

    simplifyCasts();

    // Simplify simple calculations..
    while (simplifyCalculations())
        ;

    // Replace "*(str + num)" => "str[num]"
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (! strchr(";{}(=<>", tok->aaaa0()))
            continue;

        Token *next = tok->next();
        if (! next)
            break;

        if (Token::Match(next, "* ( %var% + %num% )"))
        {
            const char *str[4] = {"var", "[", "num", "]"};
            str[0] = tok->strAt(3);
            str[2] = tok->strAt(5);

            for (int i = 0; i < 4; i++)
            {
                tok = tok->next();
                tok->str(str[i]);
            }

            tok->deleteNext();
            tok->deleteNext();
        }
    }



    // Split up variable declarations if possible..
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (! Token::Match(tok, "[{};]"))
            continue;

        Token *type0 = tok->next();
        if (!Token::Match(type0, "%type%"))
            continue;
        if (Token::Match(type0, "else|return"))
            continue;

        Token *tok2 = NULL;
        unsigned int typelen = 0;

        if (Token::Match(type0, "%type% %var% ,|="))
        {
            if (type0->next()->str() != "operator")
            {
                tok2 = type0->tokAt(2);    // The ',' or '=' token
                typelen = 1;
            }
        }

        else if (Token::Match(type0, "%type% * %var% ,|="))
        {
            if (type0->next()->next()->str() != "operator")
            {
                tok2 = type0->tokAt(3);    // The ',' token
                typelen = 1;
            }
        }

        else if (Token::Match(type0, "%type% %var% [ %num% ] ,"))
        {
            tok2 = type0->tokAt(5);    // The ',' token
            typelen = 1;
        }

        else if (Token::Match(type0, "%type% * %var% [ %num% ] ,"))
        {
            tok2 = type0->tokAt(6);    // The ',' token
            typelen = 1;
        }

        else if (Token::Match(type0, "struct %type% %var% ,|="))
        {
            tok2 = type0->tokAt(3);
            typelen = 2;
        }

        else if (Token::Match(type0, "struct %type% * %var% ,|="))
        {
            tok2 = type0->tokAt(4);
            typelen = 2;
        }


        if (tok2)
        {
            if (tok2->str() == ",")
            {
                tok2->str(";");
                InsertTokens(tok2, type0, typelen);
            }

            else
            {
                Token *eq = tok2;

                int parlevel = 0;
                while (tok2)
                {
                    if (strchr("{(", tok2->aaaa0()))
                    {
                        ++parlevel;
                    }

                    else if (strchr("})", tok2->aaaa0()))
                    {
                        if (parlevel < 0)
                            break;
                        --parlevel;
                    }

                    else if (parlevel == 0 && strchr(";,", tok2->aaaa0()))
                    {
                        // "type var ="   =>   "type var; var ="
                        Token *VarTok = type0->tokAt(typelen);
                        if (VarTok->aaaa0() == '*')
                            VarTok = VarTok->next();
                        InsertTokens(eq, VarTok, 2);
                        eq->str(";");

                        // "= x, "   =>   "= x; type "
                        if (tok2->str() == ",")
                        {
                            tok2->str(";");
                            InsertTokens(tok2, type0, typelen);
                        }
                        break;
                    }

                    tok2 = tok2->next();
                }
            }
        }
    }

    // In case variable declarations have been updated...
    setVarId();

    // Replace NULL with 0..
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (tok->str() == "NULL")
            tok->str("0");
    }

    // Replace pointer casts of 0.. "(char *)0" => "0"
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (Token::Match(tok->next(), "( %type% * ) 0") || Token::Match(tok->next(), "( %type% %type% * ) 0"))
        {
            while (!Token::simpleMatch(tok->next(), "0"))
                tok->deleteNext();
        }
    }

    simplifyIfAddBraces();
    simplifyFunctionParameters();

    elseif();

    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (Token::Match(tok, "case %any% : %var%"))
            tok->next()->next()->insertToken(";");
        if (Token::Match(tok, "default : %var%"))
            tok->next()->insertToken(";");
    }

    bool modified = true;
    while (modified)
    {
        modified = false;
        modified |= simplifyConditions();
        modified |= simplifyFunctionReturn();
        modified |= simplifyKnownVariables();
        modified |= removeReduntantConditions();
        modified |= simplifyRedundantParanthesis();
        modified |= simplifyCalculations();
    }
}
//---------------------------------------------------------------------------

const Token *Tokenizer::findClosing(const Token *tok, const char *start, const char *end)
{
    if (!tok)
        return 0;

    // Find the closing "}"
    int indentLevel = 0;
    for (const Token *closing = tok->next(); closing; closing = closing->next())
    {
        if (closing->str() == start)
        {
            ++indentLevel;
            continue;
        }

        if (closing->str() == end)
            --indentLevel;

        if (indentLevel >= 0)
            continue;

        // Closing } is found.
        return closing;
    }

    return 0;
}

bool Tokenizer::removeReduntantConditions()
{
    bool ret = false;

    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (!Token::simpleMatch(tok, "if"))
            continue;

        if (!Token::Match(tok->tokAt(1), "( %bool% ) {"))
            continue;

        // Find matching else
        const Token *elseTag = 0;

        // Find the closing "}"
        elseTag = Tokenizer::findClosing(tok->tokAt(4), "{", "}");
        if (elseTag)
            elseTag = elseTag->next();

        bool boolValue = false;
        if (tok->tokAt(2)->str() == "true")
            boolValue = true;

        // Handle if with else
        if (elseTag && elseTag->str() == "else")
        {
            if (Token::simpleMatch(elseTag->next(), "if"))
            {
                // Handle "else if"
                if (boolValue == false)
                {
                    // Convert "if( false ) {aaa;} else if() {bbb;}" => "if() {bbb;}"
                    Token::eraseTokens(tok, elseTag->tokAt(2));
                    ret = true;
                }
                else
                {
                    // Keep first if, remove every else if and else after it
                    const Token *lastTagInIf = elseTag->tokAt(2);
                    while (lastTagInIf)
                    {
                        if (lastTagInIf->str() == "(")
                        {
                            lastTagInIf = Tokenizer::findClosing(lastTagInIf, "(", ")");
                            lastTagInIf = lastTagInIf->next();
                        }

                        lastTagInIf = Tokenizer::findClosing(lastTagInIf, "{", "}");
                        lastTagInIf = lastTagInIf->next();
                        if (!Token::simpleMatch(lastTagInIf, "else"))
                            break;

                        lastTagInIf = lastTagInIf->next();
                        if (Token::simpleMatch(lastTagInIf, "if"))
                            lastTagInIf = lastTagInIf->next();
                    }

                    Token::eraseTokens(elseTag->previous(), lastTagInIf);
                    ret = true;
                }
            }
            else
            {
                // Handle else
                if (boolValue == false)
                {
                    // Convert "if( false ) {aaa;} else {bbb;}" => "{bbb;}" or ";{bbb;}"
                    if (tok->previous())
                        tok = tok->previous();
                    else
                        tok->str(";");

                    Token::eraseTokens(tok, elseTag->tokAt(1));
                }
                else
                {
                    if (Token::simpleMatch(elseTag->tokAt(1), "{"))
                    {
                        // Convert "if( true ) {aaa;} else {bbb;}" => "{aaa;}"
                        const Token *end = Tokenizer::findClosing(elseTag->tokAt(1), "{", "}");
                        if (!end)
                        {
                            // Possibly syntax error in code
                            return false;
                        }

                        // Remove the "else { aaa; }"
                        Token::eraseTokens(elseTag->previous(), end->tokAt(1));
                    }

                    // Remove "if( true )"
                    if (tok->previous())
                        tok = tok->previous();
                    else
                        tok->str(";");

                    Token::eraseTokens(tok, tok->tokAt(5));
                }

                ret = true;
            }
        }

        // Handle if without else
        else
        {
            if (boolValue == false)
            {
                // Remove if and its content
                if (tok->previous())
                    tok = tok->previous();
                else
                    tok->str(";");

                Token::eraseTokens(tok, elseTag);
            }
            else
            {
                // convert "if( true ) {aaa;}" => "{aaa;}"
                if (tok->previous())
                    tok = tok->previous();
                else
                    tok->str(";");

                Token::eraseTokens(tok, tok->tokAt(5));
            }

            ret = true;
        }
    }

    return ret;
}

bool Tokenizer::simplifyIfAddBraces()
{
    bool ret = false;

    for (Token *tok = _tokens; tok; tok = tok ? tok->next() : NULL)
    {
        if (Token::Match(tok, "if|for|while ("))
        {
            // Goto the ending ')'
            int parlevel = 1;
            tok = tok->next();
            while (parlevel >= 1 && (tok = tok->next()))
            {
                if (tok->str() == "(")
                    ++parlevel;
                else if (tok->str() == ")")
                    --parlevel;
            }

            // ')' should be followed by '{'
            if (!tok || Token::simpleMatch(tok, ") {"))
                continue;
        }

        else if (tok->str() == "else")
        {
            // An else followed by an if or brace don't need to be processed further
            if (Token::Match(tok, "else if|{"))
                continue;
        }

        else
        {
            continue;
        }

        // insert open brace..
        tok->insertToken("{");
        tok = tok->next();

        // insert close brace..
        // In most cases it would work to just search for the next ';' and insert a closing brace after it.
        // But here are special cases..
        // * if (cond) for (;;) break;
        // * if (cond1) if (cond2) { }
        int parlevel = 0;
        int indentlevel = 0;
        while ((tok = tok->next()) != NULL)
        {
            if (tok->str() == "{")
                ++indentlevel;

            else if (tok->str() == "}")
            {
                --indentlevel;
                if (indentlevel == 0)
                    break;
            }

            else if (tok->str() == "(")
                ++parlevel;

            else if (tok->str() == ")")
                --parlevel;

            else if (indentlevel == 0 && parlevel == 0 && tok->str() == ";")
                break;
        }

        if (tok)
        {
            tok->insertToken("}");
            ret = true;
        }
    }

    return ret;
}

bool Tokenizer::simplifyConditions()
{
    bool ret = false;

    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (Token::simpleMatch(tok, "( true &&") || Token::simpleMatch(tok, "&& true &&") || Token::simpleMatch(tok->next(), "&& true )"))
        {
            tok->deleteNext();
            tok->deleteNext();
            ret = true;
        }

        else if (Token::simpleMatch(tok, "( false ||") || Token::simpleMatch(tok, "|| false ||") || Token::simpleMatch(tok->next(), "|| false )"))
        {
            tok->deleteNext();
            tok->deleteNext();
            ret = true;
        }

        // Change numeric constant in condition to "true" or "false"
        if (Token::Match(tok, "if|while ( %num%") &&
            (tok->tokAt(3)->str() == ")" || tok->tokAt(3)->str() == "||" || tok->tokAt(3)->str() == "&&"))
        {
            tok->next()->next()->str((tok->tokAt(2)->str() != "0") ? "true" : "false");
            ret = true;
        }
        Token *tok2 = tok->tokAt(2);
        if (tok2                                        &&
            (tok->str() == "&&" || tok->str() == "||")  &&
            Token::Match(tok->next(), "%num%")          &&
            (tok2->str() == ")" || tok2->str() == "&&" || tok2->str() == "||"))
        {
            tok->next()->str((tok->next()->str() != "0") ? "true" : "false");
            ret = true;
        }

        // Reduce "(%num% == %num%)" => "(true)"/"(false)"
        const Token *tok4 = tok->tokAt(4);
        if (! tok4)
            break;
        if ((tok->str() == "&&" || tok->str() == "||" || tok->str() == "(") &&
            Token::Match(tok->tokAt(1), "%num% %any% %num%") &&
            (tok4->str() == "&&" || tok4->str() == "||" || tok4->str() == ")"))
        {
            double op1 = (strstr(tok->strAt(1), "0x")) ? std::strtol(tok->strAt(1), 0, 16) : std::atof(tok->strAt(1));
            double op2 = (strstr(tok->strAt(3), "0x")) ? std::strtol(tok->strAt(3), 0, 16) : std::atof(tok->strAt(3));
            std::string cmp = tok->strAt(2);

            bool result = false;
            if (cmp == "==")
                result = (op1 == op2);
            else if (cmp == "!=")
                result = (op1 != op2);
            else if (cmp == ">=")
                result = (op1 >= op2);
            else if (cmp == ">")
                result = (op1 > op2);
            else if (cmp == "<=")
                result = (op1 <= op2);
            else if (cmp == "<")
                result = (op1 < op2);
            else
                cmp = "";

            if (! cmp.empty())
            {
                tok = tok->next();
                tok->deleteNext();
                tok->deleteNext();

                tok->str(result ? "true" : "false");
                ret = true;
            }
        }
    }

    return ret;
}


bool Tokenizer::simplifyCasts()
{
    bool ret = false;
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (Token::Match(tok->next(), "( %type% *| )") || Token::Match(tok->next(), "( %type% %type% *| )"))
        {
            if (tok->isName() && tok->str() != "return")
                continue;

            // Is it a cast of some variable?
            const Token *tok2 = tok->tokAt(3);
            while (tok2 && tok2->str() != ")")
                tok2 = tok2->next();
            if (!Token::Match(tok2, ") %var%"))
                continue;

            // Remove cast..
            while (tok->next()->str() != ")")
                tok->deleteNext();
            tok->deleteNext();
            ret = true;
        }

        else if (Token::Match(tok->next(), "dynamic_cast|reinterpret_cast|const_cast|static_cast <"))
        {
            while (tok->next() && tok->next()->str() != ">")
                tok->deleteNext();
            tok->deleteNext();
            tok->deleteNext();
            Token *tok2 = tok;
            int parlevel = 0;
            while (tok2->next() && parlevel >= 0)
            {
                tok2 = tok2->next();
                if (Token::simpleMatch(tok2->next(), "("))
                    ++parlevel;
                else if (Token::simpleMatch(tok2->next(), ")"))
                    --parlevel;
            }
            if (tok2->next())
                tok2->deleteNext();

            ret = true;
        }
    }

    return ret;
}


bool Tokenizer::simplifyFunctionParameters()
{
    bool ret = false;
    int indentlevel = 0;
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (tok->str() == "{")
            ++indentlevel;

        else if (tok->str() == "}")
            --indentlevel;

        // Find the function e.g. foo( x ) or foo( x, y )
        else if (indentlevel == 0 && Token::Match(tok, "%var% ( %var% [,)]"))
        {
            // We have found old style function, now we need to change it

            // Get list of argument names
            std::map<std::string, Token*> argumentNames;
            bool bailOut = false;
            for (tok = tok->tokAt(2); tok; tok = tok->tokAt(2))
            {
                if (!Token::Match(tok, "%var% [,)]"))
                {
                    bailOut = true;
                    break;
                }

                argumentNames[tok->str()] = tok;
                if (tok->next()->str() == ")")
                {
                    tok = tok->tokAt(2);
                    break;
                }
            }

            if (bailOut)
            {
                continue;
            }

            Token *start = tok;
            while (tok && tok->str() != "{")
            {
                if (tok->str() == ";")
                {
                    tok = tok->previous();
                    // Move tokens from start to tok into the place of
                    // argumentNames[tok->str()] and remove the ";"

                    if (argumentNames.find(tok->str()) == argumentNames.end())
                    {
                        bailOut = true;
                        break;
                    }

                    // Remove the following ";"
                    Token *temp = tok->tokAt(2);
                    tok->deleteNext();

                    // Replace "x" with "int x" or similar
                    Token::replace(argumentNames[tok->str()], start, tok);
                    ret = true;
                    tok = temp;
                    start = tok;
                }
                else
                {
                    tok = tok->next();
                }
            }

            if (tok == NULL)
            {
                break;
            }

            if (bailOut)
            {
                continue;
            }

            ++indentlevel;
        }
    }

    return ret;
}


bool Tokenizer::simplifyFunctionReturn()
{
    bool ret = false;
    int indentlevel = 0;
    for (const Token *tok = tokens(); tok; tok = tok->next())
    {
        if (tok->str() == "{")
            ++indentlevel;

        else if (tok->str() == "}")
            --indentlevel;

        else if (indentlevel == 0 && Token::Match(tok, "%var% ( ) { return %num% ; }"))
        {
            std::ostringstream pattern;
            pattern << "[(=+-*/] " << tok->str() << " ( ) [;)+-*/]";
            for (Token *tok2 = _tokens; tok2; tok2 = tok2->next())
            {
                if (Token::Match(tok2, pattern.str().c_str()))
                {
                    tok2 = tok2->next();
                    tok2->str(tok->strAt(5));
                    tok2->deleteNext();
                    tok2->deleteNext();
                    ret = true;
                }
            }
        }
    }

    return ret;
}


static void incdec(std::string &value, const std::string &op)
{
    int ivalue = 0;
    std::istringstream istr(value.c_str());
    istr >> ivalue;
    if (op == "++")
        ++ivalue;
    else if (op == "--")
        --ivalue;
    std::ostringstream ostr;
    ostr << ivalue;
    value = ostr.str();
}


bool Tokenizer::simplifyKnownVariables()
{
    bool ret = false;
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        // Search for a block of code
        if (! Token::Match(tok, ") const| {"))
            continue;

        // parse the block of code..
        int indentlevel = 0;
        for (Token *tok2 = tok; tok2; tok2 = tok2->next())
        {

            if (tok2->str() == "{")
                ++indentlevel;

            else if (tok2->str() == "}")
            {
                --indentlevel;
                if (indentlevel <= 0)
                    break;
            }

            else if (Token::Match(tok2, "%var% = %num% ;") ||
                     Token::Match(tok2, "%var% = %bool% ;"))
            {
                unsigned int varid = tok2->varId();
                if (varid == 0)
                    continue;

                std::string value(tok2->strAt(2));

                for (Token *tok3 = tok2->next(); tok3; tok3 = tok3->next())
                {
                    // Perhaps it's a loop => bail out
                    if (Token::Match(tok3, "[{}]"))
                        break;

                    // Variable is used somehow in a non-defined pattern => bail out
                    if (tok3->varId() == varid)
                        break;

                    // Replace variable with numeric constant..
                    if (Token::Match(tok3, "if ( %varid% )", varid))
                    {
                        tok3 = tok3->next()->next();
                        tok3->str(value.c_str());
                        ret = true;
                    }

                    // Variable is used in calculation..
                    if (Token::Match(tok3, "[=+-*/[] %varid% [+-*/;]]", varid))
                    {
                        tok3 = tok3->next();
                        tok3->str(value.c_str());
                        ret = true;
                    }

                    if (Token::Match(tok3->next(), "%varid% ++|--", varid))
                    {
                        const std::string op(tok3->strAt(2));
                        if (Token::Match(tok3, "; %any% %any% ;"))
                        {
                            tok3->deleteNext();
                            tok3->deleteNext();
                        }
                        else
                        {
                            tok3 = tok3->next();
                            tok3->str(value.c_str());
                            tok3->deleteNext();
                        }
                        incdec(value, op);
                        tok2->tokAt(2)->str(value.c_str());
                        ret = true;
                    }

                    if (Token::Match(tok3->next(), "++|-- %varid%", varid))
                    {
                        incdec(value, tok3->strAt(1));
                        tok2->tokAt(2)->str(value.c_str());
                        if (Token::Match(tok3, "; %any% %any% ;"))
                        {
                            tok3->deleteNext();
                            tok3->deleteNext();
                        }
                        else
                        {
                            tok3->deleteNext();
                            tok3->next()->str(value.c_str());
                        }
                        tok3 = tok3->next();
                        ret = true;
                    }
                }
            }
        }
    }

    return ret;
}


bool Tokenizer::elseif()
{
    bool ret = false;
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (!Token::simpleMatch(tok, "else if"))
            continue;
        int indent = 0;
        for (Token *tok2 = tok; indent >= 0 && tok2; tok2 = tok2->next())
        {
            if (Token::Match(tok2, "(|{"))
                ++indent;
            else if (Token::Match(tok2, ")|}"))
                --indent;

            if (indent == 0 && Token::Match(tok2, "}|;"))
            {
                if (!Token::simpleMatch(tok2->next(), "else"))
                {
                    tok->insertToken("{");
                    tok2->insertToken("}");
                    ret = true;
                    break;
                }
            }
        }
    }
    return ret;
}


bool Tokenizer::simplifyRedundantParanthesis()
{
    bool ret = false;
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (Token::simpleMatch(tok, "( ("))
        {
            int parlevel = 0;
            for (Token *tok2 = tok; tok2; tok2 = tok2->next())
            {
                if (tok2->str() == "(")
                    ++parlevel;

                else if (tok2->str() == ")")
                {
                    --parlevel;
                    if (parlevel == 1)
                    {
                        if (Token::simpleMatch(tok2, ") )"))
                        {
                            tok->deleteNext();
                            tok2->deleteNext();
                        }
                        break;
                    }
                }
            }
        }
    }
    return ret;
}

bool Tokenizer::simplifyCalculations()
{
    bool ret = false;
    for (Token *tok = _tokens; tok; tok = tok->next())
    {
        if (Token::simpleMatch(tok->next(), "* 1") || Token::simpleMatch(tok->next(), "1 *"))
        {
            for (int i = 0; i < 2; i++)
                tok->deleteNext();
            ret = true;
        }

        // (1-2)
        if (Token::Match(tok, "[[,(=<>] %num% [+-*/] %num% [],);=<>]"))
        {
            int i1 = std::atoi(tok->strAt(1));
            int i2 = std::atoi(tok->strAt(3));
            if (i2 == 0 && *(tok->strAt(2)) == '/')
            {
                continue;
            }

            switch (*(tok->strAt(2)))
            {
            case '+':
                i1 += i2;
                break;
            case '-':
                i1 -= i2;
                break;
            case '*':
                i1 *= i2;
                break;
            case '/':
                i1 /= i2;
                break;
            }
            tok = tok->next();
            std::ostringstream str;
            str <<  i1;
            tok->str(str.str().c_str());
            for (int i = 0; i < 2; i++)
            {
                tok->deleteNext();
            }

            ret = true;
        }

        // Remove parantheses around number..
        if (!tok->isName() && Token::Match(tok->next(), "( %num% )"))
        {
            tok->deleteNext();
            tok = tok->next();
            tok->deleteNext();
            ret = true;
        }

        // Remove parantheses around variable..
        // keep parantheses here: dynamic_cast<Fred *>(p);
        if (!tok->isName() && tok->str() != ">" && Token::Match(tok->next(), "( %var% ) [;),+-*/><]]"))
        {
            tok->deleteNext();
            tok = tok->next();
            tok->deleteNext();
            ret = true;
        }
    }
    return ret;
}




//---------------------------------------------------------------------------
// Helper functions for handling the tokens list
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------

const Token *Tokenizer::GetFunctionTokenByName(const char funcname[]) const
{
    for (unsigned int i = 0; i < _functionList.size(); ++i)
    {
        if (_functionList[i]->str() == funcname)
        {
            return _functionList[i];
        }
    }
    return NULL;
}


void Tokenizer::fillFunctionList()
{
    _functionList.clear();

    int indentlevel = 0;
    for (const Token *tok = _tokens; tok; tok = tok->next())
    {
        if (tok->str() == "{")
            ++indentlevel;

        else if (tok->str() == "}")
            --indentlevel;

        if (indentlevel > 0)
        {
            continue;
        }

        if (Token::Match(tok, "%var% ("))
        {
            // Check if this is the first token of a function implementation..
            for (const Token *tok2 = tok->tokAt(2); tok2; tok2 = tok2->next())
            {
                if (tok2->str() == ";")
                {
                    tok = tok2;
                    break;
                }

                else if (tok2->str() == "{")
                {
                    break;
                }

                else if (tok2->str() == ")")
                {
                    if (Token::Match(tok2, ") const| {"))
                    {
                        _functionList.push_back(tok);
                        tok = tok2;
                    }
                    else
                    {
                        tok = tok2;
                        while (tok->next() && !strchr(";{", tok->next()->aaaa0()))
                            tok = tok->next();
                    }
                    break;
                }
            }
        }
    }

    // If the _functionList functions with duplicate names, remove them
    // TODO this will need some better handling
    for (unsigned int func1 = 0; func1 < _functionList.size();)
    {
        bool hasDuplicates = false;
        for (unsigned int func2 = func1 + 1; func2 < _functionList.size();)
        {
            if (_functionList[func1]->str() == _functionList[func2]->str())
            {
                hasDuplicates = true;
                _functionList.erase(_functionList.begin() + func2);
            }
            else
            {
                ++func2;
            }
        }

        if (! hasDuplicates)
        {
            ++func1;
        }
        else
        {
            _functionList.erase(_functionList.begin() + func1);
        }
    }
}

//---------------------------------------------------------------------------

// Deallocate lists..
void Tokenizer::DeallocateTokens()
{
    deleteTokens(_tokens);
    _tokens = 0;
    _tokensBack = 0;
    _files.clear();
}

void Tokenizer::deleteTokens(Token *tok)
{
    while (tok)
    {
        Token *next = tok->next();
        delete tok;
        tok = next;
    }
}

//---------------------------------------------------------------------------

const char *Tokenizer::getParameterName(const Token *ftok, int par)
{
    int _par = 1;
    for (; ftok; ftok = ftok->next())
    {
        if (ftok->str() == ",")
            ++_par;
        if (par == _par && Token::Match(ftok, "%var% [,)]"))
            return ftok->aaaa();
    }
    return NULL;
}

//---------------------------------------------------------------------------

std::string Tokenizer::fileLine(const Token *tok) const
{
    std::ostringstream ostr;
    ostr << "[" << _files.at(tok->fileIndex()) << ":" << tok->linenr() << "]";
    return ostr.str();
}

std::string Tokenizer::file(const Token *tok) const
{
    return _files.at(tok->fileIndex());
}

//---------------------------------------------------------------------------
