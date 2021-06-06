
#include <sstream>
#include <iostream>
#include <string>
#include <set>
#include <vector>
#include <map>

struct TemplateProcessor {
    TemplateProcessor(std::istream& ins, std::ostream& os, std::string name)
        : mInStream{ins}, mOutStream{os}, mClassName{std::move(name)} {}

    bool atEnd() { return mInStream.eof() || mInStream.peek() == -1; }

    void process();
    
    private:
        void handleDefault(int ch);
        void processCommand();
        void interpretPreprocessorCommand(const std::string& s);

        void closeStringIfNeeded();
        void beginStringIfNeeded();

        void buildConstructor();
        void buildGetSetFunctions();
        void buildMembers();

        bool mInString, mInlineFlag;

        std::istream& mInStream;
        std::ostream& mOutStream;
        std::stringstream mRenderCode;
        std::set<std::string> mHeaderIncludes;
        std::map<std::string, std::string> mParameters;
        std::string mClassName;
};
