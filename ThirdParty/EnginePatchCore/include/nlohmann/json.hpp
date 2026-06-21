// Stub for nlohmann/json v3.11.3
// To be downloaded from: https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
// This is a placeholder - the actual header should be downloaded separately

namespace nlohmann {
    class json {
    public:
        static json parse(const std::string& str) {
            return json();
        }
        bool is_array() const { return false; }
        bool is_string() const { return false; }
        bool is_object() const { return false; }
        template<typename T>
        T get() const { return T(); }
        json& operator[](const std::string& key) { return *this; }
        const json& operator[](const std::string& key) const { return *this; }
    };
}
