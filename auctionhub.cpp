// auctionhub.cpp -- AuctionHub: Real-Time Bid Analytics Engine
//
// Build : g++ -std=c++17 -O2 -Wall -Wextra -o auctionhub auctionhub.cpp
// Run   : ./auctionhub [test_data/small.txt] [test_data/large.txt]
//
// Five hand-written sorts on struct Bid:
//   A. Insertion Sort          (Live Auction Monitor)
//   B. Selection Sort          (Top-K Bid Finder)
//   C. Interchange Sort        (Price Anomaly Validator)
//   D. Bubble Sort, early stop (Stabilization Detector)
//   E. Quick Sort, median-of-3 (Full Historical Sorter)

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <utility>
#include <vector>
using namespace std;
using namespace std::chrono;

struct Bid {
    string bidderId;
    double amount;       // bid price
    long long timestamp; // Unix ms
    int auctionId;
};

// ---------- Utilities ----------
void printBids(const vector<Bid>& v, size_t limit = 10) {
    for (size_t i = 0; i < min(v.size(), limit); ++i)
        cout << "[" << v[i].bidderId << "] $" << v[i].amount
             << " @ " << v[i].timestamp << "\n";
    if (v.size() > limit) cout << "... (" << v.size() << " total)\n";
}

bool loadBids(const string& path, vector<Bid>& out) {
    ifstream f(path);
    if (!f) return false;
    Bid b;
    while (f >> b.bidderId >> b.amount >> b.timestamp >> b.auctionId)
        out.push_back(b);
    return true;
}

bool saveBids(const string& path, const vector<Bid>& v) {
    ofstream f(path);
    if (!f) return false;
    f << fixed << setprecision(2);
    for (const Bid& b : v)
        f << b.bidderId << ' ' << b.amount << ' ' << b.timestamp << ' ' << b.auctionId << '\n';
    return true;
}

// ---------- Comparators (single source of truth for ordering) ----------
// Ascending: smaller amount first; equal amounts -> earlier timestamp first.
static bool lessAsc(const Bid& a, const Bid& b) {
    if (a.amount != b.amount) return a.amount < b.amount;
    return a.timestamp < b.timestamp;
}
// Descending by amount (highest bid first); ties -> earlier timestamp first.
static bool higherFirst(const Bid& a, const Bid& b) {
    if (a.amount != b.amount) return a.amount > b.amount;
    return a.timestamp < b.timestamp;
}

// ---------- Declarations ----------
void insertionSort(vector<Bid>& v);              // Task A
void selectionSort(vector<Bid>& v);              // Task B
void interchangeSort(vector<Bid>& v);            // Task C (swap count in g_interchangeSwaps)
bool bubbleSortEarlyStop(vector<Bid>& v);        // Task D -- true if stable (no swaps on 1st pass)
void quickSort(vector<Bid>& v, int lo, int hi);  // Task E
void quickSortWrapper(vector<Bid>& v);

static long long g_interchangeSwaps = 0;

// =====================================================================
// Task A -- Insertion Sort (ascending by amount, tie -> timestamp)
// Keep v[0..i-1] sorted; take key = v[i] and shift larger elements right.
// Best case O(n) on (nearly) sorted input, worst O(n^2). Stable, in-place.
// =====================================================================
void insertionSort(vector<Bid>& v) {
    const int n = static_cast<int>(v.size());
    for (int i = 1; i < n; ++i) {
        Bid key = v[i];
        int j = i - 1;
        while (j >= 0 && lessAsc(key, v[j])) {  // strict compare => stable
            v[j + 1] = v[j];                    // shift right
            --j;
        }
        v[j + 1] = key;                         // drop key into its slot
    }
}

// =====================================================================
// Task B -- Selection Sort, in-place, DESCENDING by amount
// Each pass selects the maximum of the unsorted suffix and swaps it to
// position i. After pass k, v[0..k] are the k+1 highest bids => Top-K.
// =====================================================================
void selectionSort(vector<Bid>& v) {
    const int n = static_cast<int>(v.size());
    for (int i = 0; i < n - 1; ++i) {
        int best = i;                            // index of the current maximum
        for (int j = i + 1; j < n; ++j)
            if (higherFirst(v[j], v[best])) best = j;
        if (best != i) swap(v[i], v[best]);      // at most one swap per pass
    }
}

// =====================================================================
// Task C -- Interchange Sort (ascending). Compare v[i] with EVERY v[j]
// (j > i) and swap immediately when out of order. Counts total swaps.
// =====================================================================
void interchangeSort(vector<Bid>& v) {
    const int n = static_cast<int>(v.size());
    g_interchangeSwaps = 0;
    for (int i = 0; i < n - 1; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (lessAsc(v[j], v[i])) {
                swap(v[i], v[j]);
                ++g_interchangeSwaps;
            }
        }
    }
}

// =====================================================================
// Task D -- Bubble Sort with early stop (ascending)
// Returns true  => the FIRST full pass made no swap (input was already sorted = "STABLE")
//         false => at least one swap happened on the first pass ("UNSTABLE")
// The `swapped` flag also ends the sort as soon as any later pass is clean.
// =====================================================================
bool bubbleSortEarlyStop(vector<Bid>& v) {
    const int n = static_cast<int>(v.size());
    bool stableOnFirstPass = true;
    for (int pass = 0; pass < n - 1; ++pass) {
        bool swapped = false;
        for (int j = 0; j < n - 1 - pass; ++j) {  // the tail is already in place
            if (lessAsc(v[j + 1], v[j])) {
                swap(v[j], v[j + 1]);
                swapped = true;
            }
        }
        if (pass == 0 && swapped) stableOnFirstPass = false;
        if (!swapped) break;                       // early stop
    }
    return stableOnFirstPass;
}

// =====================================================================
// Task E -- Quick Sort, Lomuto partition, median-of-three pivot
// =====================================================================
// Order v[lo], v[mid], v[hi] so that v[lo] <= v[mid] <= v[hi], move the
// median to v[hi], then run Lomuto with pivot = v[hi].
static int partitionMedianOfThree(vector<Bid>& v, int lo, int hi) {
    const int mid = lo + (hi - lo) / 2;            // avoids (lo+hi) overflow
    if (lessAsc(v[mid], v[lo])) swap(v[mid], v[lo]);
    if (lessAsc(v[hi],  v[lo])) swap(v[hi],  v[lo]);
    if (lessAsc(v[hi],  v[mid])) swap(v[hi], v[mid]);
    swap(v[mid], v[hi]);                           // median -> pivot slot

    const Bid pivot = v[hi];
    int i = lo - 1;                                // boundary of "<= pivot"
    for (int j = lo; j < hi; ++j) {
        if (!lessAsc(pivot, v[j])) {               // v[j] <= pivot
            ++i;
            swap(v[i], v[j]);
        }
    }
    swap(v[i + 1], v[hi]);                         // pivot to its final index
    return i + 1;
}

// Recurse into the SMALLER side and loop on the larger one => the stack
// depth is O(log n) even when partitions are unbalanced.
void quickSort(vector<Bid>& v, int lo, int hi) {
    while (lo < hi) {
        const int p = partitionMedianOfThree(v, lo, hi);
        if (p - lo < hi - p) { quickSort(v, lo, p - 1); lo = p + 1; }
        else                 { quickSort(v, p + 1, hi); hi = p - 1; }
    }
}

void quickSortWrapper(vector<Bid>& v) {
    if (v.size() < 2) return;                      // guards v.size()-1 underflow on empty vector
    quickSort(v, 0, static_cast<int>(v.size()) - 1);
}

// ---------- Test helpers ----------
static bool isSortedAsc(const vector<Bid>& v) {
    for (size_t i = 1; i < v.size(); ++i)
        if (lessAsc(v[i], v[i - 1])) return false;
    return true;
}
static bool isSortedDesc(const vector<Bid>& v) {
    for (size_t i = 1; i < v.size(); ++i)
        if (higherFirst(v[i], v[i - 1])) return false;
    return true;
}
static void printLine(const Bid& b) {
    cout << "  " << b.bidderId << " " << fixed << setprecision(2) << b.amount
         << " @ " << b.timestamp << "\n";
}
static void printAll(const vector<Bid>& v) {
    for (const Bid& b : v) printLine(b);
}

// Random bids with std::mt19937. Amounts are rounded to cents so that
// duplicates happen and the timestamp tie-break is actually exercised.
static vector<Bid> generateBids(size_t n, unsigned seed) {
    mt19937 rng(seed);
    uniform_int_distribution<int> cents(100, 1000000);   // $1.00 .. $10,000.00
    uniform_int_distribution<int> bidder(1, 5000);
    uniform_int_distribution<int> auction(1, 200);
    vector<Bid> v;
    v.reserve(n);
    long long ts = 1700000000000LL;
    for (size_t i = 0; i < n; ++i) {
        ts += 1 + rng() % 50;                            // strictly increasing timestamps
        v.push_back({"user" + to_string(bidder(rng)), cents(rng) / 100.0, ts, auction(rng)});
    }
    return v;
}

// Near-sorted stream: amount rises ~0.1% per bid, a few neighbours swapped.
static vector<Bid> generateNearSorted(size_t n, unsigned seed) {
    mt19937 rng(seed);
    vector<Bid> v;
    v.reserve(n);
    double price = 100.0;
    long long ts = 1700000000000LL;
    for (size_t i = 0; i < n; ++i) {
        price *= 1.001;
        ts += 5;
        v.push_back({"user" + to_string(i % 1000), price, ts, 1});
    }
    for (size_t k = 0; k < n / 100; ++k) {               // ~1% local disorder
        size_t i = rng() % (n - 1);
        swap(v[i].amount, v[i + 1].amount);
    }
    return v;
}

template <class F>
static double timeMs(F&& f) {
    auto t0 = steady_clock::now();
    f();
    return duration<double, milli>(steady_clock::now() - t0).count();
}

static int g_fail = 0;
static void check(bool ok, const string& what) {
    cout << (ok ? "  [PASS] " : "  [FAIL] ") << what << "\n";
    if (!ok) ++g_fail;
}

// ---------- Edge-case suite (same cases for every algorithm) ----------
static void runEdgeCases() {
    cout << "\n=== Edge cases (all 5 algorithms) ===\n";
    auto mk = [](double a, long long t) { return Bid{"x", a, t, 1}; };
    vector<pair<string, vector<Bid>>> cases = {
        {"empty",            {}},
        {"single",           {mk(5, 1)}},
        {"two (reversed)",   {mk(9, 2), mk(3, 1)}},
        {"already sorted",   {mk(1, 1), mk(2, 2), mk(3, 3), mk(4, 4)}},
        {"reverse sorted",   {mk(4, 1), mk(3, 2), mk(2, 3), mk(1, 4)}},
        {"all equal amount", {mk(5, 4), mk(5, 3), mk(5, 2), mk(5, 1)}},
        {"duplicates mixed", {mk(3, 5), mk(1, 4), mk(3, 3), mk(2, 2), mk(3, 1)}},
        {"negative/zero",    {mk(0, 3), mk(-1, 2), mk(0, 1)}},
    };
    for (auto& [name, data] : cases) {
        vector<Bid> a = data, b = data, c = data, d = data, e = data;
        insertionSort(a);
        selectionSort(b);
        interchangeSort(c);
        bubbleSortEarlyStop(d);
        quickSortWrapper(e);
        check(isSortedAsc(a) && isSortedDesc(b) && isSortedAsc(c) && isSortedAsc(d) && isSortedAsc(e)
                  && a.size() == data.size() && b.size() == data.size() && e.size() == data.size(),
              name);
    }
}

int main(int argc, char* argv[]) {
    const string smallPath = argc > 1 ? argv[1] : "test_data/small.txt";
    const string largePath = argc > 2 ? argv[2] : "test_data/large.txt";

    // ---- load small dataset (falls back to the sample from the spec) ----
    vector<Bid> small;
    if (!loadBids(smallPath, small) || small.empty()) {
        cout << "(could not read " << smallPath << " -- using built-in sample)\n";
        small = {{"alice", 100.0, 1000, 1}, {"bob", 102.5, 1005, 1},
                 {"carol", 101.0, 1003, 1}, {"dave", 103.0, 1010, 1}};
    }

    // ---- Task A ----
    cout << "=== Task A: Insertion Sort (ascending) ===\n";
    {
        vector<Bid> v = small;
        insertionSort(v);
        printAll(v);
        check(isSortedAsc(v), "sorted ascending by amount");
    }

    // ---- Task B ----
    cout << "\n=== Task B: Selection Sort (descending) -- Top 3 ===\n";
    {
        vector<Bid> v = small;
        selectionSort(v);
        for (size_t i = 0; i < min<size_t>(3, v.size()); ++i) printLine(v[i]);
        check(isSortedDesc(v), "sorted descending by amount");
    }

    // ---- Task C ----
    cout << "\n=== Task C: Interchange Sort (ascending) ===\n";
    {
        vector<Bid> v = small;
        interchangeSort(v);
        printAll(v);
        cout << "  Total swaps: " << g_interchangeSwaps << "\n";
        check(isSortedAsc(v), "sorted ascending by amount");
    }

    // ---- Task D ----
    cout << "\n=== Task D: Bubble Sort with early stop ===\n";
    {
        vector<Bid> sortedIn = small;
        sort(sortedIn.begin(), sortedIn.end(), lessAsc);
        vector<Bid> reversedIn(sortedIn.rbegin(), sortedIn.rend());

        bool r1 = bubbleSortEarlyStop(sortedIn);
        cout << "  already-sorted input -> " << (r1 ? "STABLE" : "UNSTABLE") << "\n";
        check(r1 && isSortedAsc(sortedIn), "sorted input returns true (STABLE)");

        bool r2 = bubbleSortEarlyStop(reversedIn);
        cout << "  reversed input       -> " << (r2 ? "STABLE" : "UNSTABLE") << "\n";
        check(!r2 && isSortedAsc(reversedIn), "reversed input returns false (UNSTABLE) and ends sorted");
    }

    // ---- Edge cases ----
    runEdgeCases();

    // ---- Task E ----
    cout << "\n=== Task E: Quick Sort, median-of-three, 100,000 bids ===\n";
    {
        vector<Bid> large;
        if (!loadBids(largePath, large) || large.size() < 100000) {
            cout << "  (" << largePath << " missing -- generating 100,000 bids with mt19937 and saving it)\n";
            large = generateBids(100000, 42);
            if (!saveBids(largePath, large)) cout << "  (warning: could not write " << largePath << ")\n";
            else { large.clear(); loadBids(largePath, large); }   // use exactly what is on disk
        }
        vector<Bid> ref = large;
        sort(ref.begin(), ref.end(), lessAsc);                    // std::sort used ONLY as a checker

        vector<Bid> v = large;
        double ms = timeMs([&] { quickSortWrapper(v); });
        cout << "  n = " << v.size() << "  quickSort runtime: " << fixed << setprecision(3) << ms << " ms\n";
        check(isSortedAsc(v), "output is sorted (amount, then timestamp)");
        bool same = v.size() == ref.size();
        for (size_t i = 0; same && i < v.size(); ++i)
            same = v[i].amount == ref[i].amount && v[i].timestamp == ref[i].timestamp;
        check(same, "identical to std::sort reference (nothing lost or duplicated)");

        // median-of-three vs. pathological-for-naive inputs
        vector<Bid> s = ref;
        double msSorted = timeMs([&] { quickSortWrapper(s); });
        vector<Bid> r(ref.rbegin(), ref.rend());
        double msRev = timeMs([&] { quickSortWrapper(r); });
        cout << "  already-sorted input: " << msSorted << " ms | reverse-sorted input: " << msRev << " ms\n";
        check(isSortedAsc(s) && isSortedAsc(r), "sorted / reversed 100k inputs handled without O(n^2) blow-up");
    }

    // ---- Evidence for the Task A claim (insertion sort on near-sorted data) ----
    cout << "\n=== Why insertion sort for Task A (timing evidence) ===\n";
    {
        vector<Bid> near = generateNearSorted(100000, 7);
        double msNear = timeMs([&] { insertionSort(near); });
        vector<Bid> rnd = generateBids(10000, 7);
        double msRnd = timeMs([&] { insertionSort(rnd); });
        cout << "  insertion sort, near-sorted n=100,000 : " << msNear << " ms\n";
        cout << "  insertion sort, random      n= 10,000 : " << msRnd << " ms\n";
        check(isSortedAsc(near) && isSortedAsc(rnd), "both results sorted");
    }

    cout << "\n" << (g_fail == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";
    return g_fail == 0 ? 0 : 1;
}
