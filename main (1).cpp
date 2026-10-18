#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

uint64_t demoHash(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;

    for (unsigned char c : s) {
        h = (h ^ c) * 1099511628211ULL;
    }

    return h;
}

struct UserRecord {
    std::string username;
    std::string salt;
    uint64_t passwordHash{};
};

enum class CollisionStrategy {
    Chaining,
    LinearProbing,
    QuadraticProbing,
    DoubleHashing
};

std::string strategyName(CollisionStrategy strategy) {
    switch (strategy) {
        case CollisionStrategy::Chaining:
            return "separate chaining";

        case CollisionStrategy::LinearProbing:
            return "linear probing";

        case CollisionStrategy::QuadraticProbing:
            return "quadratic probing";

        case CollisionStrategy::DoubleHashing:
            return "double hashing";
    }

    return "unknown";
}

template <typename V>
class HashTable {
private:
    struct Entry {
        std::string key;
        V value;
    };

    struct Slot {
        std::optional<Entry> entry;
        bool deleted = false;
    };

    CollisionStrategy strategy_;

    std::vector<std::vector<Entry>> chains_;
    std::vector<Slot> slots_;

    std::size_t size_ = 0;
    std::size_t rehashCount_ = 0;

    static constexpr double MAX_LOAD = 0.75;

    std::size_t primaryIndex(
        const std::string& key,
        std::size_t capacity
    ) const {
        return static_cast<std::size_t>(
            demoHash(key) % capacity
        );
    }

    std::size_t secondStep(
        const std::string& key,
        std::size_t capacity
    ) const {
        return 1 + static_cast<std::size_t>(
            demoHash("step:" + key) % (capacity - 1)
        );
    }

    std::size_t probeIndex(
        const std::string& key,
        std::size_t attempt,
        std::size_t capacity
    ) const {
        const std::size_t start =
            primaryIndex(key, capacity);

        if (strategy_ == CollisionStrategy::LinearProbing) {
            return (start + attempt) % capacity;
        }

        if (strategy_ == CollisionStrategy::QuadraticProbing) {
            return (start + attempt * attempt) % capacity;
        }

        return (
            start + attempt * secondStep(key, capacity)
        ) % capacity;
    }

    static std::size_t nextPrime(std::size_t value) {
        auto isPrime = [](std::size_t number) {
            if (number < 2) {
                return false;
            }

            for (std::size_t divisor = 2;
                 divisor * divisor <= number;
                 ++divisor) {
                if (number % divisor == 0) {
                    return false;
                }
            }

            return true;
        };

        while (!isPrime(value)) {
            ++value;
        }

        return value;
    }

    void initialize(std::size_t capacity) {
        capacity = nextPrime(capacity);

        if (strategy_ == CollisionStrategy::Chaining) {
            chains_.assign(capacity, {});
        } else {
            slots_.assign(capacity, {});
        }
    }

    void insertWithoutResize(
        const std::string& key,
        const V& value
    ) {
        if (strategy_ == CollisionStrategy::Chaining) {
            auto& bucket =
                chains_[primaryIndex(key, chains_.size())];

            for (auto& item : bucket) {
                if (item.key == key) {
                    item.value = value;
                    return;
                }
            }

            bucket.push_back({key, value});
            ++size_;
            return;
        }

        for (std::size_t attempt = 0;
             attempt < slots_.size();
             ++attempt) {
            const std::size_t index =
                probeIndex(key, attempt, slots_.size());

            Slot& slot = slots_[index];

            if (slot.entry.has_value() &&
                slot.entry->key == key) {
                slot.entry->value = value;
                return;
            }

            if (!slot.entry.has_value()) {
                slot.entry = Entry{key, value};
                slot.deleted = false;
                ++size_;
                return;
            }
        }

        throw std::runtime_error(
            "Hash table is full; resize failed."
        );
    }

    void rehash(std::size_t requestedCapacity) {
        std::vector<Entry> entries;

        if (strategy_ == CollisionStrategy::Chaining) {
            for (const auto& bucket : chains_) {
                for (const auto& item : bucket) {
                    entries.push_back(item);
                }
            }
        } else {
            for (const auto& slot : slots_) {
                if (slot.entry.has_value()) {
                    entries.push_back(*slot.entry);
                }
            }
        }

        initialize(requestedCapacity);
        size_ = 0;

        for (const auto& item : entries) {
            insertWithoutResize(item.key, item.value);
        }

        ++rehashCount_;
    }

    void growIfNeeded() {
        const std::size_t capacity =
            strategy_ == CollisionStrategy::Chaining
                ? chains_.size()
                : slots_.size();

        if (static_cast<double>(size_ + 1) / capacity >
            MAX_LOAD) {
            rehash(capacity * 2 + 1);
        }
    }

public:
    explicit HashTable(
        CollisionStrategy strategy,
        std::size_t initialCapacity = 5
    )
        : strategy_(strategy) {
        initialize(initialCapacity);
    }

    bool insert(
        const std::string& key,
        const V& value
    ) {
        growIfNeeded();

        const std::size_t oldSize = size_;

        insertWithoutResize(key, value);

        return size_ != oldSize;
    }

    V* find(const std::string& key) {
        if (strategy_ == CollisionStrategy::Chaining) {
            auto& bucket =
                chains_[primaryIndex(key, chains_.size())];

            for (auto& item : bucket) {
                if (item.key == key) {
                    return &item.value;
                }
            }

            return nullptr;
        }

        for (std::size_t attempt = 0;
             attempt < slots_.size();
             ++attempt) {
            const std::size_t index =
                probeIndex(key, attempt, slots_.size());

            Slot& slot = slots_[index];

            if (!slot.entry.has_value() &&
                !slot.deleted) {
                return nullptr;
            }

            if (slot.entry.has_value() &&
                slot.entry->key == key) {
                return &slot.entry->value;
            }
        }

        return nullptr;
    }

    const V* find(const std::string& key) const {
        return const_cast<HashTable*>(this)->find(key);
    }

    bool remove(const std::string& key) {
        if (strategy_ == CollisionStrategy::Chaining) {
            auto& bucket =
                chains_[primaryIndex(key, chains_.size())];

            for (auto it = bucket.begin();
                 it != bucket.end();
                 ++it) {
                if (it->key == key) {
                    bucket.erase(it);
                    --size_;
                    return true;
                }
            }

            return false;
        }

        for (std::size_t attempt = 0;
             attempt < slots_.size();
             ++attempt) {
            const std::size_t index =
                probeIndex(key, attempt, slots_.size());

            Slot& slot = slots_[index];

            if (!slot.entry.has_value() &&
                !slot.deleted) {
                return false;
            }

            if (slot.entry.has_value() &&
                slot.entry->key == key) {
                slot.entry.reset();
                slot.deleted = true;
                --size_;
                return true;
            }
        }

        return false;
    }

    double loadFactor() const {
        const std::size_t capacity =
            strategy_ == CollisionStrategy::Chaining
                ? chains_.size()
                : slots_.size();

        return static_cast<double>(size_) / capacity;
    }

    std::size_t size() const {
        return size_;
    }

    std::size_t capacity() const {
        return strategy_ == CollisionStrategy::Chaining
            ? chains_.size()
            : slots_.size();
    }

    std::size_t rehashCount() const {
        return rehashCount_;
    }

    CollisionStrategy strategy() const {
        return strategy_;
    }
};

class AuthenticationSystem {
private:
    HashTable<UserRecord> users_;

    const std::string pepper_ =
        "classroom-pepper";

    unsigned int nextSalt_ = 1001;

    std::string makeSalt() {
        return "salt-" +
               std::to_string(nextSalt_++);
    }

    uint64_t passwordHash(
        const std::string& salt,
        const std::string& password
    ) const {
        return demoHash(
            salt + password + pepper_
        );
    }

public:
    AuthenticationSystem()
        : users_(CollisionStrategy::Chaining, 5) {}

    void registerUser(
        const std::string& username,
        const std::string& password
    ) {
        const std::string salt = makeSalt();

        UserRecord record{
            username,
            salt,
            passwordHash(salt, password)
        };

        users_.insert(username, record);
    }

    bool login(
        const std::string& username,
        const std::string& password
    ) const {
        const UserRecord* record =
            users_.find(username);

        if (record == nullptr) {
            return false;
        }

        return passwordHash(
            record->salt,
            password
        ) == record->passwordHash;
    }

    bool removeUser(
        const std::string& username
    ) {
        return users_.remove(username);
    }

    std::size_t tableSize() const {
        return users_.size();
    }

    std::size_t capacity() const {
        return users_.capacity();
    }

    double loadFactor() const {
        return users_.loadFactor();
    }

    std::size_t rehashCount() const {
        return users_.rehashCount();
    }
};

void printLoginResult(
    const std::string& label,
    bool result
) {
    std::cout << std::left
              << std::setw(32)
              << label
              << (result ? "SUCCESS" : "FAILURE")
              << '\n';
}

int main() {
    std::cout
        << "CS 210 Assignment 06: "
        << "Authentication Demo\n\n";

    std::cout
        << "Collision strategies implemented:\n";

    for (const auto strategy : {
        CollisionStrategy::Chaining,
        CollisionStrategy::LinearProbing,
        CollisionStrategy::QuadraticProbing,
        CollisionStrategy::DoubleHashing
    }) {
        HashTable<int> table(strategy, 5);

        table.insert("alpha", 1);
        table.insert("bravo", 2);
        table.insert("charlie", 3);

        std::cout
            << "- "
            << strategyName(strategy)
            << ": find(alpha) = "
            << *table.find("alpha")
            << ", load factor = "
            << std::fixed
            << std::setprecision(2)
            << table.loadFactor()
            << '\n';
    }

    std::cout
        << "\nChosen strategy for user records: "
        << "separate chaining\n";

    std::cout
        << "Reason: it handles collisions cleanly, "
        << "supports deletion naturally, and remains "
        << "flexible as the table grows.\n\n";

    AuthenticationSystem auth;

    auth.registerUser("alice", "Maple123!");
    auth.registerUser("brandon", "River456!");
    auth.registerUser("carla", "Stone789!");
    auth.registerUser("diego", "Cloud321!");
    auth.registerUser("elena", "Cedar654!");

    std::cout << "Authentication tests:\n";

    printLoginResult(
        "Correct password for alice",
        auth.login("alice", "Maple123!")
    );

    printLoginResult(
        "Wrong password for alice",
        auth.login("alice", "wrong")
    );

    printLoginResult(
        "Unknown user",
        auth.login("frank", "Anything123!")
    );

    std::cout
        << "\nTable before removal: size = "
        << auth.tableSize()
        << ", capacity = "
        << auth.capacity()
        << ", load factor = "
        << std::fixed
        << std::setprecision(2)
        << auth.loadFactor()
        << '\n';

    std::cout
        << "Remove diego: "
        << (
            auth.removeUser("diego")
                ? "SUCCESS"
                : "FAILURE"
        )
        << '\n';

    printLoginResult(
        "Login after removing diego",
        auth.login("diego", "Cloud321!")
    );

    std::cout
        << "\nRehash events triggered: "
        << auth.rehashCount()
        << '\n';

    std::cout
        << "Final table: size = "
        << auth.tableSize()
        << ", capacity = "
        << auth.capacity()
        << ", load factor = "
        << std::fixed
        << std::setprecision(2)
        << auth.loadFactor()
        << '\n';

    std::cout
        << "\nNote: demoHash is included for "
        << "data-structures practice only. "
        << "Production systems should use a "
        << "password-specific KDF such as Argon2id, "
        << "bcrypt, scrypt, or PBKDF2.\n";

    return 0;
}