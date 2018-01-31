#ifndef CPPJIEBA_MPSEGMENT_H
#define CPPJIEBA_MPSEGMENT_H

#include <algorithm>
#include <set>
#include <cassert>
#include <regex>
#include "limonp/Logging.hpp"
#include "DictTrie.hpp"
#include "SegmentTagged.hpp"
#include "PosTagger.hpp"

namespace cppjieba {

class MPSegment: public SegmentTagged {
 public:
  MPSegment(const string& dictPath, const string& userDictPath = "")
    : dictTrie_(new DictTrie(dictPath, userDictPath)), isNeedDestroy_(true) {
  }
  MPSegment(const DictTrie* dictTrie)
    : dictTrie_(dictTrie), isNeedDestroy_(false) {
    assert(dictTrie_);
  }
  ~MPSegment() {
    if (isNeedDestroy_) {
      delete dictTrie_;
    }
  }

  void Cut(const string& sentence, vector<string>& words) const {
    Cut(sentence, words, MAX_WORD_LENGTH);
  }

  void Cut(const string& sentence,
        vector<string>& words,
        size_t max_word_len) const {
    vector<Word> tmp;
    Cut(sentence, tmp, max_word_len);
    GetStringsFromWords(tmp, words);
  }
  void Cut(const string& sentence, 
        vector<Word>& words, 
        size_t max_word_len = MAX_WORD_LENGTH) const {
    PreFilter pre_filter(symbols_, sentence);
    PreFilter::Range range;
    vector<WordRange> wrs;
    wrs.reserve(sentence.size()/2);
    while (pre_filter.HasNext()) {
      range = pre_filter.Next();
      Cut(range.begin, range.end, wrs, max_word_len);
    }
    words.clear();
    words.reserve(wrs.size());
    GetWordsFromWordRanges(sentence, wrs, words);
  }
  void Cut(RuneStrArray::const_iterator begin,
           RuneStrArray::const_iterator end,
           vector<WordRange>& words,
           size_t max_word_len = MAX_WORD_LENGTH) const {
    vector<Dag> dags;
    dictTrie_->Find(begin, 
          end, 
          dags,
          max_word_len);
    /* test
    std::cerr.precision(17);
    for (vector<Dag>::const_iterator it = dags.begin(); it != dags.end(); it++) {
      std::cerr << it->runestr.rune << ' ' << it->runestr.offset << ' ' <<it->runestr.len;
      std::cerr << it->weight;
      for (LocalVector<pair<size_t, const DictUnit*> >::const_iterator nit = it->nexts.begin(); nit != it->nexts.end(); nit++) {
        if (nit->second) {
          std::cerr << " nexts:" << nit->first << ' ' << nit->second->weight;
        }
      }
      std::cerr << endl;
      // XLOG(ERROR) << it->runestr << it->weight << it->pInfo->word << it->pInfo->weight;
    }
    */
    CalcDP(dags);
    /* test
    for (vector<Dag>::const_iterator it = dags.begin(); it != dags.end(); it++) {
      std::cerr << it->nextPos << " " << it->weight;
      if (it->pInfo) {
        std::cerr << " " << it->pInfo->word.size() << endl;
      } else {
        std::cerr << " null" << endl;
      }
    }
    std::cerr << dictTrie_->GetMinWeight() << endl;
    */
    CutByDag(begin, end, dags, words);
  }

  const DictTrie* GetDictTrie() const {
    return dictTrie_;
  }

  bool Tag(const string& src, vector<pair<string, string> >& res) const {
    return tagger_.Tag(src, res, *this);
  }

  bool IsUserDictSingleChineseWord(const Rune& value) const {
    return dictTrie_->IsUserDictSingleChineseWord(value);
  }
 private:
  void CalcDP(vector<Dag>& dags) const {
    size_t nextPos;
    const DictUnit* p;
    double val;

    for (vector<Dag>::reverse_iterator rit = dags.rbegin(); rit != dags.rend(); rit++) {
      rit->pInfo = NULL;
      rit->weight = MIN_DOUBLE;
      assert(!rit->nexts.empty());
      for (LocalVector<pair<size_t, const DictUnit*> >::const_iterator it = rit->nexts.begin(); it != rit->nexts.end(); it++) {
        nextPos = it->first;
        p = it->second;
        val = 0.0;
        if (nextPos + 1 < dags.size()) {
          val += dags[nextPos + 1].weight;
        }

        if (p) {
          val += p->weight;
        } else {
          val += dictTrie_->GetMinWeight();
        }
        // test std::cerr << val << " ";
        if (val >= rit->weight) {
          rit->pInfo = p;
          rit->weight = val;
          rit->nextPos = nextPos;
        }
      }
      // test std::cerr << endl;
    }
  }
  void CutByDag(RuneStrArray::const_iterator begin, 
        RuneStrArray::const_iterator end, 
        const vector<Dag>& dags, 
        vector<WordRange>& words) const {
    size_t i = 0;
    size_t buf_len = 0;
    while (i < dags.size()) {
      const DictUnit* p = dags[i].pInfo;
      if (p) {
        assert(p->word.size() >= 1);
        if (buf_len > 0) {
          WordRange wr(begin + i - buf_len, begin + i - 1);
          words.push_back(wr);
          buf_len = 0;
        }
        WordRange wr(begin + i, begin + i + p->word.size() - 1);
        words.push_back(wr);
        i += p->word.size();
      } else { //single chinese word
        // deal with alnum character
        const uint32_t c = dags[i].runestr.rune;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
          buf_len += 1;
          // test std::cerr << "aaa " << i << " " << c << " " << buf_len << endl;
          i++;
          continue;
        }
        if (buf_len > 0) {
          // test std::cerr << "alnum " << i << " " << buf_len << endl;
          WordRange wr(begin + i - buf_len, begin + i - 1);
          words.push_back(wr);
          buf_len = 0;
        }
        WordRange wr(begin + i, begin + i);
        words.push_back(wr);
        i++;
      }
    }
    if (buf_len > 0) {
      WordRange wr(begin + i - buf_len, begin + i - 1);
      words.push_back(wr);
      buf_len = 0;
    }
  }
  const DictTrie* dictTrie_;
  bool isNeedDestroy_;
  PosTagger tagger_;
}; // class MPSegment

} // namespace cppjieba

#endif
