namespace ReduceSaveSize {
    std::string compressWithLibdeflate(const std::string& input, int level = 12);
    std::string compressWithLibdeflateParallel(const std::string& input, int level = 12);
}