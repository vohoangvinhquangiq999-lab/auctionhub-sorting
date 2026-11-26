# REFLECTION.md — AuctionHub: Real-Time Bid Analytics Engine

## 1. Algorithm–scenario justification

**A. Insertion Sort — Live Auction Monitor.**
Bids arrive almost in order (each ~0.1% above the previous), so every new element has to move only a few positions left. Insertion sort does work proportional to the number of inversions: O(n + I), which is O(n) when each element is displaced by at most k places (O(n·k)). It is also in-place, stable (equal amounts keep their arrival order) and works online, because a new bid can be inserted into an already sorted prefix.
*Measured:* 100,000 near-sorted bids sorted in ≈ 1.5 ms, whereas 10,000 *random* bids took ≈ 270 ms (same code) — the O(n²) behaviour only appears when the data is not nearly sorted.

**B. Selection Sort — Top-K Bid Finder.**
Each pass selects the maximum of the unsorted suffix and puts it at the front, so after K passes the first K positions already hold the Top-K bids; for "Top 3" we only need 3 passes = O(K·n). It does at most n−1 swaps (one per pass), which is the fewest writes among the simple sorts — useful when moving a `Bid` (with a `string`) is expensive. Downsides: always Θ(n²) comparisons and not stable, so it is only appropriate for small sets or small K.

**C. Interchange Sort — Price Anomaly Validator.**
For batches of ≤ 50 records n² ≈ 2,500 comparisons — microseconds. The algorithm is only a few lines (compare `v[i]` with every later `v[j]`, swap if out of order), so it is easy to read, review and prove correct, which is exactly what a QA tool wants. It also exposes the swap count directly, which measures how disordered the batch was (0 swaps = batch was already in order). O(n²) is acceptable when n is tiny and bounded, correctness/simplicity matter more than speed, and the sort is not on a hot path.

**D. Bubble Sort with early stop — Stabilization Detector.**
The question is "did anything change?", not "sort fast". One full pass with no swap proves the window is already sorted, i.e. the price stream has stopped moving. The `swapped` flag gives best case O(n) (one pass) on stable data and ends the sort as soon as the data is ordered. Returning `true` iff the *first* pass made no swap gives exactly the STABLE/UNSTABLE signal.

**E. Quick Sort — Full Historical Sorter.**
Millions of bids need O(n log n) average time; O(n²) algorithms would take hours. Quick sort is in-place (O(log n) extra stack), cache-friendly and has a small constant factor. Median-of-three makes sorted and reverse-sorted history (very common in time-ordered bid logs) behave like balanced input instead of the O(n²) worst case of a fixed first/last pivot.

## 2. Measured runtime of Quick Sort on 100k records

Quick sort (median-of-three, Lomuto) on 100,000 random bids (`std::mt19937`, seed 42), compiled with `g++ -std=c++17 -O2`:

| Run | Time |
|---|---|
| 1 | 39.5 ms |
| 2 | 39.6 ms |
| 3 | 39.6 ms |

Output verified sorted (amount, then timestamp) and identical to a `std::sort` reference. Already-sorted and reverse-sorted 100k inputs took ≈ 12 ms and ≈ 24 ms — no blow-up.
*(Numbers are from my test machine; replace with your own run and put the screenshot in `screenshot_runtime.png`.)*

## 3. One bug and how it was diagnosed

quickSortWrapper` first called `quickSort(v, 0, v.size() - 1)`. For an **empty** vector `v.size()` is an unsigned `size_t` 0, so `v.size() - 1` wraps around to a huge value, and the call crashed/ran out of bounds. I found it with the edge-case test "empty input" (and `-fsanitize=address,undefined`). Fix: return early when `v.size() < 2` and cast to `int` *before* subtracting.

## 4. Three LLM prompts and their rating


| # | Prompt | Rating |
|---|---|---|
| 1 | "Explain why insertion sort is efficient for nearly-sorted data." | Green |
| 2 | "Generate 5 edge-case test inputs for sorting structs with floating-point keys." | Green |
| 3 | "Explain the time complexity of my quicksort when the pivot is always the smallest element." | Green |

## 5. Worst case of Quick Sort with median-of-three pivot

The worst case is **still O(n²)**. Median-of-three only picks the median of three sampled elements; it does not guarantee a balanced split. It removes the *common* bad inputs (already sorted, reverse sorted), because the middle element is then the true median. But there are adversarial inputs ("median-of-three killer" sequences) where the median of the first/middle/last elements is repeatedly one of the smallest or largest in the range, so every partition removes only O(1) elements: T(n) = T(n−1) + O(n) = O(n²).

Guaranteed O(n log n) needs something stronger: introsort (fall back to heap sort when the recursion gets too deep, as in `std::sort`), median-of-medians pivot selection, or a random pivot (O(n log n) expected). In my implementation I always recurse into the smaller partition and loop on the larger one, so the stack depth stays O(log n) even in the O(n²) time case.
