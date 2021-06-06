
#include <iomanip>
#include <iterator>
#include <algorithm>

#include "TemplateProcessor.h"

namespace {
    void trim(std::string& source)
    {
        source.erase(source.begin(), std::find_if(source.begin(), source.end(), [](char c) {
            return !isspace(static_cast<unsigned char>(c));
        }));
        source.erase(std::find_if(source.rbegin(), source.rend(), [](char c) {
            return !isspace(static_cast<unsigned char>(c));
        }).base(), source.end());
    }

    bool isRef(const std::string& s) {
        return s[s.length() - 1] == '&';
    }

    bool isPtr(const std::string& s) {
        return s[s.length() - 1] == '*';
    }
}

void TemplateProcessor::process()
{
    mHeaderIncludes.insert("<HTMLTemplate.h>");

    mInlineFlag = true;
    while (!atEnd())
    {
        int c = mInStream.get();

        if (c == '<' && mInStream.peek() == '%')
        {
            mInStream.get();
            processCommand();
            continue;
        }

        beginStringIfNeeded();
        handleDefault(c);
    }

    closeStringIfNeeded();

    mOutStream << "#ifndef _HTML_" << mClassName << "_HPP" << std::endl;
    mOutStream << "#define _HTML_" << mClassName << "_HPP" << std::endl << std::endl;

    for (const auto& c : mHeaderIncludes)
        mOutStream << "#include " << c << std::endl;

    mOutStream << "\nnamespace templates {" << std::endl;

    mOutStream << "\nstruct " << mClassName << " : HTMLTemplate {" << std::endl;

    buildConstructor();

    mOutStream << R"(
    std::string render() const override {
        std::stringstream __result;

)";
    mOutStream << mRenderCode.str();
    mOutStream << R"(
        return __result.str();
    }
)";
    
    buildGetSetFunctions();
    buildMembers();

    mOutStream << "};" << std::endl << "\n}\n#endif" << std::endl;
}

void TemplateProcessor::processCommand()
{
    std::stringstream ss;

    int modifierChar = -1;

    while (true)
    {
        if (atEnd()) throw std::runtime_error("syntax error: template command not closed");

        int ch = mInStream.get();

        if (ch == '%' && mInStream.peek() == '>')
        {
            mInStream.get();

            while (mInStream.peek() == '\n')
                mInStream.get();

            break;
        }

        if (modifierChar == -1)
        {
            if (isspace(ch)) continue;
            modifierChar = ch;

            if (ch == '-' || ch == '=' || ch == '@')
                continue;
            
            modifierChar = 0;
        }

        ss << (char)ch;
    }

    std::string _s = ss.str();
    trim(_s);

    mInlineFlag = false;
    switch (modifierChar)
    {
        case '-':
            if (mInString)
                mRenderCode << "\" << escapeHTML(" << _s << ") << \"";
            else
                mRenderCode << "        __result << escapeHTML(" << _s << ");" << std::endl;
            return;
        case '=':
            if (mInString)
                mRenderCode << "\" << " << _s << " << \"";
            else
                mRenderCode << "        __result << " << _s << ";" << std::endl;
            return;
        case '@':
            interpretPreprocessorCommand(_s);
            return;
        default:
            closeStringIfNeeded();
            mRenderCode << "        " << _s << std::endl;
            return;
    }

    //std::cout << "\n| command [" << _s << "], modifier [" << (char)modifierChar << "]\n\n";
}

void TemplateProcessor::interpretPreprocessorCommand(const std::string& s)
{
    std::vector<std::string> parts;

    std::istringstream iss(s);
    std::string tmp;
    while (getline(iss, tmp, ' '))
        parts.push_back(tmp);

    if (parts[0] == "include")
    {
        std::string includeedThing = parts[1];
        trim(includeedThing);

        char ch = includeedThing[0];
        if (ch != '<' && ch != '"')
            throw std::runtime_error("Bad include syntax");

        if (ch == '"' && ch != includeedThing[includeedThing.length() - 1])
            throw std::runtime_error("Bad include syntax");

        if (ch == '<' && '>' != includeedThing[includeedThing.length() - 1])
            throw std::runtime_error("Bad include syntax");

        mHeaderIncludes.emplace(std::move(includeedThing));
        return;
    }

    if (parts[0] == "param")
    {
        mParameters.insert({parts[2], parts[1]});
        return;
    }

    std::cout << parts.size() << std::endl;
}

void TemplateProcessor::closeStringIfNeeded()
{
    if (!mInString) return;

    mRenderCode << "\";" << std::endl;
    mInString = false;
}

void TemplateProcessor::beginStringIfNeeded()
{
    if (mInString) return;

    mRenderCode << "        __result << \"";
    mInString = true;
}

void TemplateProcessor::buildConstructor()
{
    if (mParameters.empty())
        return;

    mOutStream << "    " << mClassName << "(";

    bool flag = true;
    for (const auto& x : mParameters)
    {
        if (!flag) mOutStream << ",";
        else        flag = false;

        mOutStream << x.second << " _" << x.first;
    }

    mOutStream << ")";

    flag = true;
    mOutStream << std::endl << "        : ";

    for (const auto& x : mParameters)
    {
        if (!flag) mOutStream << ",";
        else        flag = false;

        mOutStream << x.first << "(_" << x.first << ")";
    }

    mOutStream << " {}" << std::endl;
}

void TemplateProcessor::buildGetSetFunctions()
{
    for (const auto& x : mParameters)
    {
        mOutStream << std::endl;

        std::string passType = "const " + x.second + "&";
        std::string  resType = "const " + x.second + "&";

        if (isRef(x.second) || isPtr(x.second))
        {
            passType = x.second;
            resType  = "const " + x.second;
        }

        mOutStream << "    " << resType << " get" << (char)std::toupper(x.first[0]) << x.first.substr(1) << "() const noexcept { return " << x.first << "; }\n";
        mOutStream << "    void set" << (char)std::toupper(x.first[0]) << x.first.substr(1) << "(" << passType << " _" << x.first << ") { " << x.first << " = _" << x.first << "; }\n";
    }
}

void TemplateProcessor::buildMembers()
{
    if (mParameters.empty())
        return;

    mOutStream << "\n    protected:" << std::endl;
    for (const auto& x : mParameters)
        mOutStream << "        " << x.second << " " << x.first << ";\n";
}


void TemplateProcessor::handleDefault(int ch)
{
    switch (ch)
    {
        case '\r': mRenderCode << "\\r"; break;
        case '\n':
            mRenderCode << "\\n";
            if (!mInlineFlag) closeStringIfNeeded();
            mInlineFlag = true;
            break;
        case '\f': mRenderCode << "\\f"; break;
        case '\t': mRenderCode << "\\t"; break;
        case '\v': mRenderCode << "\\v"; break;
        case '\0': mRenderCode << "\\0"; break;
        case '\"': mRenderCode << "\\\""; break;
        case '\\': mRenderCode << "\\\\"; break;
        case '\a': mRenderCode << "\\a"; break;
        case '\b': mRenderCode << "\\b"; break;
        
        default:
            if (ch > 0x7F || ch < 0x20) {
                mRenderCode << "\\x" << std::setfill('0') << std::setw(2) << std::hex << ch;
                break;
            }

            mRenderCode << (char)ch;
            break;
    }
}
