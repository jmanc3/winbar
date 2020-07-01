//
// Created by jmanc3 on 2/3/20.
//

#include "icons.h"

/**
 *
 *
 *
 * THE CODE THAT ACTUALLY FINDS THE ICONS BASED ON A WM_CLASS IS
 * AT THE BOTTOM OF THIS FILE AND IS ABOUT 200~ LOC.
 * EVERYTHING ABOVE THAT IS INIREADER OR RAPIDCSV, WHICH WE USE
 * TO PARSE THE ICON INDEX FILES AND THE ICON PATH FIXER FILE
 *
 *
 *
 */

#include <utility>
#include <vector>

/*
 * rapidcsv.h
 *
 * URL:      https://github.com/d99kris/rapidcsv
 * Version:  4.1
 *
 * Copyright (C) 2017-2019 Kristofer Berggren
 * All rights reserved.
 *
 * rapidcsv is distributed under the BSD 3-Clause license, see LICENSE for
 * details.
 *
 */

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <typeinfo>

namespace rapidcsv {

    static const bool sPlatformHasCR = false;

    struct ConverterParams {
        explicit ConverterParams(
                const bool pHasDefaultConverter = false,
                const long double pDefaultFloat = std::numeric_limits<long double>::signaling_NaN(),
                const long long pDefaultInteger = 0)
                : mHasDefaultConverter(pHasDefaultConverter), mDefaultFloat(pDefaultFloat),
                  mDefaultInteger(pDefaultInteger) {}

        bool mHasDefaultConverter;

        long double mDefaultFloat;

        long long mDefaultInteger;
    };

    class no_converter : public std::exception {

        const char *what() const noexcept override { return "unsupported conversion datatype"; }
    };

    template<typename T>
    class Converter {
    public:
        explicit Converter(const ConverterParams &pConverterParams)
                : mConverterParams(pConverterParams) {}

        void ToVal(const std::string &pStr, T &pVal) const {
            try {
                if (typeid(T) == typeid(int)) {
                    pVal = static_cast<T>(std::stoi(pStr));
                    return;
                } else if (typeid(T) == typeid(long)) {
                    pVal = static_cast<T>(std::stol(pStr));
                    return;
                } else if (typeid(T) == typeid(long long)) {
                    pVal = static_cast<T>(std::stoll(pStr));
                    return;
                } else if (typeid(T) == typeid(unsigned)) {
                    pVal = static_cast<T>(std::stoul(pStr));
                    return;
                } else if (typeid(T) == typeid(unsigned long)) {
                    pVal = static_cast<T>(std::stoul(pStr));
                    return;
                } else if (typeid(T) == typeid(unsigned long long)) {
                    pVal = static_cast<T>(std::stoull(pStr));
                    return;
                }
            } catch (...) {
                if (!mConverterParams.mHasDefaultConverter) {
                    throw;
                } else {
                    pVal = static_cast<T>(mConverterParams.mDefaultInteger);
                    return;
                }
            }

            try {
                if (typeid(T) == typeid(float)) {
                    pVal = static_cast<T>(std::stof(pStr));
                    return;
                } else if (typeid(T) == typeid(double)) {
                    pVal = static_cast<T>(std::stod(pStr));
                    return;
                } else if (typeid(T) == typeid(long double)) {
                    pVal = static_cast<T>(std::stold(pStr));
                    return;
                }
            } catch (...) {
                if (!mConverterParams.mHasDefaultConverter) {
                    throw;
                } else {
                    pVal = static_cast<T>(mConverterParams.mDefaultFloat);
                    return;
                }
            }

            if (typeid(T) == typeid(char)) {
                pVal = static_cast<T>(pStr[0]);
                return;
            } else {
                throw no_converter();
            }
        }

    private:
        const ConverterParams &mConverterParams;
    };

    template<>
    inline void
    Converter<std::string>::ToVal(const std::string &pStr, std::string &pVal) const {
        pVal = pStr;
    }

    struct LabelParams {
        explicit LabelParams(const int pColumnNameIdx = 0, const int pRowNameIdx = 0)
                : mColumnNameIdx(pColumnNameIdx), mRowNameIdx(pRowNameIdx) {}

        int mColumnNameIdx;

        int mRowNameIdx;
    };

    struct SeparatorParams {
        explicit SeparatorParams(const char pSeparator = ',',
                                 const bool pTrim = false,
                                 const bool pHasCR = sPlatformHasCR)
                : mSeparator(pSeparator), mTrim(pTrim), mHasCR(pHasCR) {}

        char mSeparator;

        bool mTrim;

        bool mHasCR;
    };

    class Document {
    public:
        explicit Document(std::string pPath = std::string(),
                          const LabelParams &pLabelParams = LabelParams(),
                          const SeparatorParams &pSeparatorParams = SeparatorParams(),
                          const ConverterParams &pConverterParams = ConverterParams())
                : mPath(std::move(pPath)), mLabelParams(pLabelParams), mSeparatorParams(pSeparatorParams),
                  mConverterParams(pConverterParams) {
            if (!mPath.empty()) {
                ReadCsv();
            }
        }

        Document(const Document &pDocument)
                : mPath(pDocument.mPath), mLabelParams(pDocument.mLabelParams),
                  mSeparatorParams(pDocument.mSeparatorParams), mConverterParams(pDocument.mConverterParams),
                  mData(pDocument.mData), mColumnNames(pDocument.mColumnNames), mRowNames(pDocument.mRowNames) {}

        template<typename T>
        std::vector<T> GetColumn(const size_t pColumnIdx) const {
            const ssize_t columnIdx = pColumnIdx + (mLabelParams.mRowNameIdx + 1);
            std::vector<T> column;
            Converter<T> converter(mConverterParams);
            for (auto itRow = mData.begin(); itRow != mData.end(); ++itRow) {
                if (std::distance(mData.begin(), itRow) > mLabelParams.mColumnNameIdx) {
                    T val;
                    converter.ToVal(itRow->at(columnIdx), val);
                    column.push_back(val);
                }
            }
            return column;
        }

        template<typename T>
        std::vector<T> GetColumn(const std::string &pColumnName) const {
            const ssize_t columnIdx = GetColumnIdx(pColumnName);
            if (columnIdx < 0) {
                throw std::out_of_range("column not found: " + pColumnName);
            }
            return GetColumn<T>(columnIdx);
        }

        template<typename T>
        T GetCell(const size_t pColumnIdx, const size_t pRowIdx) const {
            const ssize_t columnIdx = pColumnIdx + (mLabelParams.mRowNameIdx + 1);
            const ssize_t rowIdx = pRowIdx + (mLabelParams.mColumnNameIdx + 1);

            T val;
            Converter<T> converter(mConverterParams);
            converter.ToVal(mData.at(rowIdx).at(columnIdx), val);
            return val;
        }

        template<typename T>
        T GetCell(const std::string &pColumnName, const size_t pRowIdx) const {
            const ssize_t columnIdx = GetColumnIdx(pColumnName);
            if (columnIdx < 0) {
                throw std::out_of_range("column not found: " + pColumnName);
            }

            return GetCell<T>(columnIdx, pRowIdx);
        }

    private:
        void ReadCsv() {
            std::ifstream stream;
            stream.exceptions(std::ifstream::failbit | std::ifstream::badbit);
            stream.open(mPath, std::ios::binary);
            {
                stream.seekg(0, std::ios::beg);
                ReadCsv(stream);
            }
        }

        void ReadCsv(std::istream &pStream) {
            pStream.seekg(0, std::ios::end);
            std::streamsize fileLength = pStream.tellg();
            pStream.seekg(0, std::ios::beg);
            const std::streamsize bufLength = 64 * 1024;
            std::vector<char> buffer(bufLength);
            std::vector<std::string> row;
            std::string cell;
            bool quoted = false;
            int cr = 0;
            int lf = 0;

            while (fileLength > 0) {
                std::streamsize readLength = std::min(fileLength, bufLength);
                pStream.read(buffer.data(), readLength);
                for (int i = 0; i < readLength; ++i) {
                    if (buffer[i] == '"') {
                        if (cell.empty() || cell[0] == '"') {
                            quoted = !quoted;
                        }
                        cell += buffer[i];
                    } else if (buffer[i] == mSeparatorParams.mSeparator) {
                        if (!quoted) {
                            row.push_back(mSeparatorParams.mTrim ? Trim(cell) : cell);
                            cell.clear();
                        } else {
                            cell += buffer[i];
                        }
                    } else if (buffer[i] == '\r') {
                        ++cr;
                    } else if (buffer[i] == '\n') {
                        ++lf;
                        row.push_back(mSeparatorParams.mTrim ? Trim(cell) : cell);
                        cell.clear();
                        mData.push_back(row);
                        row.clear();
                        quoted = false; // disallow line breaks in quoted string, by
                        // auto-unquote at linebreak
                    } else {
                        cell += buffer[i];
                    }
                }
                fileLength -= readLength;
            }

            // Handle last line without linebreak
            if (!cell.empty() || !row.empty()) {
                row.push_back(mSeparatorParams.mTrim ? Trim(cell) : cell);
                cell.clear();
                mData.push_back(row);
                row.clear();
            }

            // Assume CR/LF if at least half the linebreaks have CR
            mSeparatorParams.mHasCR = (cr > (lf / 2));

            // Set up column labels
            if ((mLabelParams.mColumnNameIdx >= 0) && (!mData.empty())) {
                int i = 0;
                for (auto &columnName : mData[mLabelParams.mColumnNameIdx]) {
                    mColumnNames[columnName] = i++;
                }
            }

            // Set up row labels
            if ((mLabelParams.mRowNameIdx >= 0) &&
                (static_cast<ssize_t>(mData.size()) > (mLabelParams.mColumnNameIdx + 1))) {
                int i = 0;
                for (auto &dataRow : mData) {
                    mRowNames[dataRow[mLabelParams.mRowNameIdx]] = i++;
                }
            }
        }

        ssize_t GetColumnIdx(const std::string &pColumnName) const {
            if (mLabelParams.mColumnNameIdx >= 0) {
                if (mColumnNames.find(pColumnName) != mColumnNames.end()) {
                    return mColumnNames.at(pColumnName) - (mLabelParams.mRowNameIdx + 1);
                }
            }
            return -1;
        }

        static std::string Trim(const std::string &pStr) {
            std::string str = pStr;

            // ltrim
            str.erase(str.begin(),
                      std::find_if(str.begin(), str.end(), [](int ch) { return !isspace(ch); }));

            // rtrim
            str.erase(
                    std::find_if(str.rbegin(), str.rend(), [](int ch) { return !isspace(ch); }).base(),
                    str.end());

            return str;
        }

    private:
        std::string mPath;
        LabelParams mLabelParams;
        SeparatorParams mSeparatorParams;
        ConverterParams mConverterParams;
        std::vector<std::vector<std::string>> mData;
        std::map<std::string, size_t> mColumnNames;
        std::map<std::string, size_t> mRowNames;
#ifdef HAS_CODECVT
        bool mIsUtf16 = false;
        bool mIsLE = false;
#endif
    };
}

/**
 * My stuff
 */

#include "INIReader.h"
#include "utility.h"
#include <chrono>
#include <cstring>
#include <dirent.h>

bool
get_current_theme_name(std::string *active_theme) {
    std::string gtk_settings_file_path(getenv("HOME"));
    gtk_settings_file_path += "/.config/gtk-3.0/settings.ini";

    INIReader gtk_settings(gtk_settings_file_path);
    if (gtk_settings.ParseError() != 0) {
        return false;
    }

    *active_theme = gtk_settings.Get("Settings", "gtk-icon-theme-name", "hicolor");

    return true;
}

// Icon names and wm_classes are not always the same so for some applications we
// need to manually fix the correlation with the file found below. Almost no one
// does this right, but we do. https://github.com/Foggalong/hardcode-fixer
//
std::string
fix_wm_class(const std::string &wm_class) {
    std::string hard_code_fixer(getenv("HOME"));
    hard_code_fixer += "/.config/winbar/tofix.csv";

    try {
        rapidcsv::Document doc(hard_code_fixer, rapidcsv::LabelParams(0, -1));

        std::vector<std::string> launchers = doc.GetColumn<std::string>("Launcher");

        int i = 0;
        for (std::string launcher : launchers) {
            std::for_each(launcher.begin(), launcher.end(), [](char &c) { c = std::tolower(c); });

            if (launcher == wm_class) {
                return doc.GetCell<std::string>("Icon Name", i);
            }
            i++;
        }
    } catch (const std::exception &ex) {
        return wm_class;
    }
    return wm_class;
}

static inline bool
prefix(const char *pre, const char *str) {
    return strncmp(pre, str, strlen(pre)) == 0;
}

std::string
find_icon(std::string wm_class, int size, bool recursive, std::string theme_name) {
    // read that themes index file to find apps folder at correct size
    std::string base_icon_path("/usr/share/icons/");
    std::string theme_index_file_path = base_icon_path + theme_name + "/index.theme";

    INIReader theme_index(theme_index_file_path);
    if (theme_index.ParseError() != 0) {
        //        std::cout << "Coudn't parse index.theme file for current theme: "
        //        << theme_index_file_path << std::endl;
        return "";
    }

    for (auto section : theme_index.Sections()) {
        auto Context = theme_index.Get(section, "Context", "");
        if (Context != "Applications")
            continue;

        auto Size = theme_index.Get(section, "Size", "");
        if (Size != std::to_string(size))
            continue;

        // try to find icon
        wm_class = fix_wm_class(wm_class);
        wm_class += "."; // so we only find exact matches with file_name

        std::string path = base_icon_path + theme_name + "/" + section;

        const char *file_name_c = wm_class.c_str();
        const char *path_c = path.c_str();

        DIR *dp;
        if ((dp = opendir(path_c)) == NULL)
            return "";

        struct dirent *dirp;
        while ((dirp = readdir(dp)) != NULL) {
            if (!prefix(file_name_c, dirp->d_name) != 0)
                continue;

            closedir(dp);

            // if found return it
            return std::string(path + "/" + dirp->d_name);
        }

        closedir(dp);

        break;
    }

    if (recursive) {
        // if not found check the fallback themes one layer deep
        auto Inherits = theme_index.Get("Icon Theme", "Inherits", "");
        std::stringstream ss(Inherits);
        std::vector<std::string> result;

        while (ss.good()) {
            std::string substr;
            getline(ss, substr, ',');
            result.push_back(substr);
        }

        for (auto str : result) {
            auto path = find_icon(wm_class, size, false, str);
            if (path != "") {
                return "";
            }
        }
    }

    return "";
}

std::string
find_icon(std::string wm_class, int size) {
    // find current set theme
    std::string theme_name;
    bool found_current_theme = get_current_theme_name(&theme_name);
    if (!found_current_theme) {
        std::cout << "Couldn't find current GTK theme" << std::endl;
        return "";
    }
    return find_icon(wm_class, size, true, theme_name);
}