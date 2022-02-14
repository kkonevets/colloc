# colloc
Extraction of stable phrases (bigrams and trigrams), counters of occurrences of phrases/lemmas and individual words/lemmas in the corpus of books. Together with lemma combinations, inflectional forms corresponding to their word combinations are extracted, that is, each lemma combination corresponds to a set of word combinations in a natural way, along with their counteractions.

The main entry point is the `extract.cpp` file, whose functions are documented in the code.
```
./colloc_extract corpus_dir save_dir
```

`corpus_dir` contains compressed text files. The result is files in the output folder `save_dir`.\
the main parameters are:
1) threshold by the number of participants in meetings of lemma combinations `threshold` (function `group_lem2/3`)\
2) the threshold `th1` according to the composition of documents, containing the lemma combination and the probabilistic threshold `th2`, which determines whether the phrase is stable, which is calculated by the formula below (the `filter_bilems/trilems` function).

Sample output file for bigrams:

```
00f0a0bdb1 00efbba6 1384.324108337
                              burning bush 328
                              burning bushes 1
                              burning bush 5
                              burning bush 269
                              burning bush 925
                              burning bushes 19
                              burning bush 263
                              burning bush 1
                              burning bush 24
                              burning bush 1421
```

`00f0a0bdb1 00efbba6` is a lemma combination, `1384.324108337` is a sign of the lemma combination stability (the higher, the more stable), `328` is the number of times the phrase `unburnt kup` occurs in all documents.

Accumulation calculations on a machine with NVMe disk and 16 GB RAM and in total it takes about 6 hours to get bigrams and trigrams together. An SSD drive is desirable, but will be significantly slower when converting words to identifiers (the `convert` function)\
The formula for calculating the probability of a joint meeting of words in a phrase (bigram) is taken from the article https://arxiv.org/abs/1310.4546 paragraph 4. `score = (Wij - threshold)/(Wi*Wj)` The same formula is easily extended to the case trigram.\
There is a toy example with a zero threshold in the `example.txt` file, which allows you to understand how everything works on the fingers.\
The `convert.pl` script has functions that convert Libruks zip archives from fb2 to text files. The already converted files are in `searchdev:/mnt/LibruksTxt`, specifically in this folder related to `corpus_dir`: `./colloc_extract corpus_dir save_dir`.\
For fast serialization/deserialization on disk records, the `capnp` library is used, which is several times faster than `protobuf`. This is especially useful when iterating over a corpus that contains a large binary file.\
There is also a `gramcat` utility for viewing binary files, which accepts several parameters.
``sh
gramcat uni.bin |rg "^(and|also)\s+"
gramcatlemid.bin | rg "^(00f0ad8192|00f0ad8cac)\s+"
gramcat bifiltered.bin | pr "^4222130\s+6552893"
gramcat bi.bin |rg "^22\s+8256\s+"
gramcat bifiltered.bin uni.bin lemid.bin | rg "a\s+also"
```
where `rg` is `ripgrep`\
She, depending on the type of file, selects the function for parsing. The type of filtering at the beginning of the file itself.


# install capnp

``sh
curl -O https://capnproto.org/capnproto-c++-0.9.1.tar.gz
tar zxf capnproto-c++-0.9.1.tar.gz
cd capnproto-c++-0.9.1
./tune
do -j6 check
sudo do install
```
