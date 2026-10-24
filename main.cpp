#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <queue>
#include <unordered_map>
#include <cstdint>
#include <iomanip>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

// ============================================================================
// SECTION 1: HUFFMAN TREE DATA STRUCTURES
// ============================================================================

// // HuffmanNode represents an individual node in the binary Huffman Tree.
// // It can either be a leaf node (holding a character) or an internal node
// // (combining two child subtrees).
struct HuffmanNode {
    uint8_t character;       // Byte value (0-255) stored in leaf nodes
    uint64_t frequency;      // Occurrence count of the character or subtree
    uint8_t minChar;         // Smallest character in this subtree (used for deterministic tie-breaking)
    HuffmanNode* left;       // Pointer to left child (represents bit '0')
    HuffmanNode* right;      // Pointer to right child (represents bit '1')

    // // Constructor for leaf nodes: stores the character and its frequency.
    HuffmanNode(uint8_t ch, uint64_t freq)
        : character(ch), frequency(freq), minChar(ch), left(nullptr), right(nullptr) {}

    // // Constructor for internal nodes: combines two child subtrees with their sum of frequencies.
    HuffmanNode(uint64_t freq, uint8_t minC, HuffmanNode* l, HuffmanNode* r)
        : character(0), frequency(freq), minChar(minC), left(l), right(r) {}

    // // Recursive destructor to automatically clean up tree memory and prevent leaks.
    ~HuffmanNode() {
        delete left;
        delete right;
    }

    // // Helper method to check if the node is a leaf (contains an actual character).
    bool isLeaf() const {
        return left == nullptr && right == nullptr;
    }
};

// // CompareNode is a functor used by std::priority_queue to build a Min-Heap.
// // Nodes with smaller frequencies are given higher priority (pop first).
// // If frequencies are equal, deterministic tie-breaking compares minChar.
struct CompareNode {
    bool operator()(const HuffmanNode* a, const HuffmanNode* b) const {
        // // Primary criterion: lowest frequency comes first
        if (a->frequency != b->frequency) {
            return a->frequency > b->frequency;
        }
        // // Secondary criterion: deterministic tie-breaker based on minimum character
        return a->minChar > b->minChar;
    }
};

// ============================================================================
// SECTION 2: BIT-LEVEL I/O STREAMS (BIT PACKING & UNPACKING)
// ============================================================================

// // BitWriter buffers variable-length Huffman bit codes into 8-bit bytes (uint8_t)
// // so they can be written directly to disk as raw binary data.
class BitWriter {
private:
    std::ofstream& out;  // Target binary output file stream
    uint8_t buffer;      // 8-bit accumulator holding bits until full
    int bitCount;        // Number of bits currently stored in the buffer (0 to 8)

public:
    explicit BitWriter(std::ofstream& stream)
        : out(stream), buffer(0), bitCount(0) {}

    // // Writes a single bit (0 or 1) into the byte accumulator.
    void writeBit(int bit) {
        // Shift existing bits to the left and insert the new bit at the LSB position
        buffer = (buffer << 1) | (bit & 1);
        bitCount++;

        // When 8 bits are accumulated, write the full byte to disk and reset
        if (bitCount == 8) {
            out.put(static_cast<char>(buffer));
            buffer = 0;
            bitCount = 0;
        }
    }

    // // Iterates through a binary string (e.g., "10110") and writes each bit.
    void writeBits(const std::string& bitString) {
        for (char c : bitString) {
            writeBit(c == '1' ? 1 : 0);
        }
    }

    // // Flushes any remaining bits in the buffer when encoding finishes.
    // // Left-shifts remaining bits so the final partial byte is properly padded.
    void flush() {
        if (bitCount > 0) {
            buffer <<= (8 - bitCount); // Pad remaining trailing bits with zeros
            out.put(static_cast<char>(buffer));
            buffer = 0;
            bitCount = 0;
        }
    }
};

// // BitReader reads raw binary bytes from disk and provides bit-by-bit access
// // for traversing the Huffman tree during decompression.
class BitReader {
private:
    std::ifstream& in;   // Source binary input file stream
    uint8_t buffer;      // 8-bit byte currently being unpacked
    int bitPos;          // Next bit position to extract (7 down to 0, or -1 if empty)

public:
    explicit BitReader(std::ifstream& stream)
        : in(stream), buffer(0), bitPos(-1) {}

    // // Reads the next bit from the stream.
    // // Returns 0 or 1, or -1 if the end of file (EOF) is reached.
    int readBit() {
        // If the current byte buffer has been exhausted, read the next byte from disk
        if (bitPos < 0) {
            int nextByte = in.get();
            if (nextByte == EOF) {
                return -1; // End of file
            }
            buffer = static_cast<uint8_t>(nextByte);
            bitPos = 7; // Reset to the most significant bit (MSB)
        }

        // Extract the bit at bitPos (from MSB to LSB)
        int bit = (buffer >> bitPos) & 1;
        bitPos--;
        return bit;
    }
};

// ============================================================================
// SECTION 3: HUFFMAN ALGORITHM CORE LOGIC
// ============================================================================

// // Builds the Huffman Tree from an array of 256 character frequencies.
// // Uses a min-priority queue to iteratively merge the two least frequent nodes.
HuffmanNode* buildHuffmanTree(const uint64_t freq[256]) {
    // // Step 1: Initialize min-priority queue
    std::priority_queue<HuffmanNode*, std::vector<HuffmanNode*>, CompareNode> pq;

    for (int i = 0; i < 256; ++i) {
        if (freq[i] > 0) {
            pq.push(new HuffmanNode(static_cast<uint8_t>(i), freq[i]));
        }
    }

    // // Handle edge case: empty file (no characters)
    if (pq.empty()) {
        return nullptr;
    }

    // // Handle edge case: file contains only 1 unique character.
    // // A single node has no branches; we create a dummy parent so the character gets code "0".
    if (pq.size() == 1) {
        HuffmanNode* onlyLeaf = pq.top();
        pq.pop();
        HuffmanNode* root = new HuffmanNode(onlyLeaf->frequency, onlyLeaf->minChar, onlyLeaf, nullptr);
        return root;
    }

    // // Step 2: Iteratively combine the two lowest-frequency subtrees
    while (pq.size() > 1) {
        // Extract the two lowest frequency nodes
        HuffmanNode* leftChild = pq.top();
        pq.pop();
        HuffmanNode* rightChild = pq.top();
        pq.pop();

        // Create an internal parent node with combined frequency
        uint64_t combinedFreq = leftChild->frequency + rightChild->frequency;
        uint8_t combinedMinChar = std::min(leftChild->minChar, rightChild->minChar);

        HuffmanNode* parent = new HuffmanNode(combinedFreq, combinedMinChar, leftChild, rightChild);
        pq.push(parent);
    }

    // // The final remaining node is the root of the complete Huffman Tree
    return pq.top();
}

// // Traverses the Huffman Tree recursively to generate prefix-free binary codes.
// // Going left appends '0', going right appends '1'.
void generateHuffmanCodes(const HuffmanNode* node, const std::string& currentCode, std::string codes[256]) {
    if (!node) return;

    // // If a leaf node is reached, record the accumulated binary code for this character
    if (node->isLeaf()) {
        // If the tree consisted of only 1 character with dummy parent, code is "0"
        codes[node->character] = currentCode.empty() ? "0" : currentCode;
        return;
    }

    // // Traverse left branch (bit 0)
    generateHuffmanCodes(node->left, currentCode + "0", codes);

    // // Traverse right branch (bit 1)
    generateHuffmanCodes(node->right, currentCode + "1", codes);
}

// // Helper function to print an ASCII / character representation cleanly for students
std::string formatChar(uint8_t ch) {
    switch (ch) {
        case '\n': return "'\\n' (Newline)";
        case '\r': return "'\\r' (Carriage Return)";
        case '\t': return "'\\t' (Tab)";
        case ' ':  return "' '  (Space)";
        default:
            if (ch >= 32 && ch <= 126) {
                return std::string("'") + static_cast<char>(ch) + "'";
            } else {
                return "0x" + ([ch]() {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "%02X", ch);
                    return std::string(buf);
                })();
            }
    }
}

// // Prints the Huffman Codebook table and frequency breakdown for analysis.
void displayCodeTable(const uint64_t freq[256], const std::string codes[256], uint64_t totalBytes) {
    std::cout << "\n========================================================================\n";
    std::cout << "                      HUFFMAN CODEBOOK TABLE                           \n";
    std::cout << "========================================================================\n";
    std::cout << std::left << std::setw(6)  << "Byte"
              << std::setw(24) << "Character"
              << std::setw(14) << "Frequency"
              << std::setw(12) << "Percentage"
              << std::setw(18) << "Huffman Code"
              << std::setw(8)  << "Length" << "\n";
    std::cout << "------------------------------------------------------------------------\n";

    for (int i = 0; i < 256; ++i) {
        if (freq[i] > 0) {
            double pct = (totalBytes > 0) ? (static_cast<double>(freq[i]) / totalBytes * 100.0) : 0.0;
            std::cout << std::left << std::setw(6)  << i
                      << std::setw(24) << formatChar(static_cast<uint8_t>(i))
                      << std::setw(14) << freq[i]
                      << std::fixed << std::setprecision(2) << std::setw(10) << pct << "% "
                      << std::setw(18) << codes[i]
                      << std::setw(8)  << codes[i].length() << "\n";
        }
    }
    std::cout << "========================================================================\n\n";
}

// ============================================================================
// SECTION 4: FILE COMPRESSION
// ============================================================================

// // Compresses an input text file into a binary .huff file.
// // Encapsulates metadata (original file size, unique character frequency table)
// // alongside the bit-packed payload.
bool compressFile(const std::string& inputPath, const std::string& outputPath) {
    // // Step 1: Open source file in binary mode
    std::ifstream inFile(inputPath, std::ios::binary);
    if (!inFile.is_open()) {
        std::cerr << "[-] Error: Failed to open input file: " << inputPath << "\n";
        return false;
    }

    // // Step 2: Character Frequency Analysis
    // Count exact occurrences of each byte across the entire file
    uint64_t freq[256] = {0};
    uint64_t originalSize = 0;
    char buffer[4096];

    while (inFile.read(buffer, sizeof(buffer)) || inFile.gcount() > 0) {
        std::streamsize bytesRead = inFile.gcount();
        originalSize += bytesRead;
        for (std::streamsize i = 0; i < bytesRead; ++i) {
            freq[static_cast<uint8_t>(buffer[i])]++;
        }
    }

    // Count how many unique characters appeared in the file
    uint16_t uniqueCharCount = 0;
    for (int i = 0; i < 256; ++i) {
        if (freq[i] > 0) {
            uniqueCharCount++;
        }
    }

    std::cout << "[+] Character frequency analysis completed.\n";
    std::cout << "    Original File Size: " << originalSize << " bytes\n";
    std::cout << "    Unique Characters:  " << uniqueCharCount << "\n";

    // // Step 3: Handle empty file edge case
    if (originalSize == 0) {
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile.is_open()) {
            std::cerr << "[-] Error: Cannot create output file: " << outputPath << "\n";
            return false;
        }
        // Write header indicating 0 bytes
        const char magic[4] = {'H', 'U', 'F', '1'};
        outFile.write(magic, 4);
        uint64_t zeroSize = 0;
        uint16_t zeroUniq = 0;
        outFile.write(reinterpret_cast<const char*>(&zeroSize), sizeof(zeroSize));
        outFile.write(reinterpret_cast<const char*>(&zeroUniq), sizeof(zeroUniq));
        outFile.close();
        std::cout << "[!] Input file is empty. Generated empty compressed archive.\n";
        return true;
    }

    // // Step 4: Build Huffman Tree from frequency counts
    HuffmanNode* root = buildHuffmanTree(freq);

    // // Step 5: Generate prefix-free variable-length binary codes
    std::string codes[256];
    generateHuffmanCodes(root, "", codes);

    // Display the codebook table for review
    displayCodeTable(freq, codes, originalSize);

    // // Step 6: Prepare output file and write header metadata
    std::ofstream outFile(outputPath, std::ios::binary);
    if (!outFile.is_open()) {
        std::cerr << "[-] Error: Cannot create output file: " << outputPath << "\n";
        delete root;
        return false;
    }

    // // Header Structure:
    // 1. Magic Bytes: "HUF1" (4 bytes) - Identifies valid Huffman archive
    // 2. Original File Size: uint64_t (8 bytes) - Prevents decoding trailing padding bits
    // 3. Unique Character Count: uint16_t (2 bytes) - Number of entries in frequency table
    // 4. Frequency Table Entries: [uint8_t character, uint64_t count] * uniqueCharCount
    const char magic[4] = {'H', 'U', 'F', '1'};
    outFile.write(magic, 4);
    outFile.write(reinterpret_cast<const char*>(&originalSize), sizeof(originalSize));
    outFile.write(reinterpret_cast<const char*>(&uniqueCharCount), sizeof(uniqueCharCount));

    for (int i = 0; i < 256; ++i) {
        if (freq[i] > 0) {
            uint8_t ch = static_cast<uint8_t>(i);
            uint64_t count = freq[i];
            outFile.write(reinterpret_cast<const char*>(&ch), sizeof(ch));
            outFile.write(reinterpret_cast<const char*>(&count), sizeof(count));
        }
    }

    // // Step 7: Rewind input file and encode content into bitstream
    inFile.clear();               // Clear EOF and error flags
    inFile.seekg(0, std::ios::beg); // Rewind to file beginning

    BitWriter bitWriter(outFile);
    while (inFile.read(buffer, sizeof(buffer)) || inFile.gcount() > 0) {
        std::streamsize bytesRead = inFile.gcount();
        for (std::streamsize i = 0; i < bytesRead; ++i) {
            uint8_t ch = static_cast<uint8_t>(buffer[i]);
            bitWriter.writeBits(codes[ch]);
        }
    }

    // Flush any remaining partial byte
    bitWriter.flush();

    // Close files
    inFile.close();
    outFile.close();

    // Clean up allocated tree memory
    delete root;

    // // Step 8: Compression Statistics Calculation
    uint64_t compressedSize = fs::file_size(outputPath);
    double spaceSaved = 0.0;
    if (originalSize > 0) {
        spaceSaved = (1.0 - (static_cast<double>(compressedSize) / static_cast<double>(originalSize))) * 100.0;
    }

    std::cout << "\n========================================================================\n";
    std::cout << "                      COMPRESSION SUMMARY REPORT                        \n";
    std::cout << "========================================================================\n";
    std::cout << "  Original File:          " << inputPath << "\n";
    std::cout << "  Compressed File:        " << outputPath << "\n";
    std::cout << "  Original File Size:     " << originalSize << " bytes\n";
    std::cout << "  Compressed File Size:   " << compressedSize << " bytes\n";
    std::cout << "  Space Saved:            " << std::fixed << std::setprecision(2) << spaceSaved << " %\n";
    std::cout << "  Compression Ratio:      " << std::fixed << std::setprecision(2)
              << (compressedSize > 0 ? (static_cast<double>(originalSize) / compressedSize) : 0.0) << " : 1\n";
    std::cout << "========================================================================\n\n";

    return true;
}

// ============================================================================
// SECTION 5: FILE DECOMPRESSION
// ============================================================================

// // Decompresses an encoded .huff file back into the exact original text.
// // Reads header metadata, reconstructs the identical Huffman tree,
// // and traverses the tree bit-by-bit to decode original characters.
bool decompressFile(const std::string& inputPath, const std::string& outputPath) {
    // // Step 1: Open compressed file in binary mode
    std::ifstream inFile(inputPath, std::ios::binary);
    if (!inFile.is_open()) {
        std::cerr << "[-] Error: Failed to open compressed file: " << inputPath << "\n";
        return false;
    }

    // // Step 2: Validate Header Magic Identifier
    char magic[4];
    if (!inFile.read(magic, 4) || magic[0] != 'H' || magic[1] != 'U' || magic[2] != 'F' || magic[3] != '1') {
        std::cerr << "[-] Error: Invalid file format or corrupted header. Expected 'HUF1'.\n";
        return false;
    }

    // // Step 3: Read original file size and unique character count
    uint64_t originalSize = 0;
    uint16_t uniqueCharCount = 0;
    inFile.read(reinterpret_cast<char*>(&originalSize), sizeof(originalSize));
    inFile.read(reinterpret_cast<char*>(&uniqueCharCount), sizeof(uniqueCharCount));

    // Handle empty file edge case
    if (originalSize == 0) {
        std::ofstream outFile(outputPath, std::ios::binary);
        outFile.close();
        std::cout << "[+] Decompression completed: original file was empty (0 bytes).\n";
        return true;
    }

    // // Step 4: Reconstruct frequency table from header metadata
    uint64_t freq[256] = {0};
    for (uint16_t i = 0; i < uniqueCharCount; ++i) {
        uint8_t ch = 0;
        uint64_t count = 0;
        inFile.read(reinterpret_cast<char*>(&ch), sizeof(ch));
        inFile.read(reinterpret_cast<char*>(&count), sizeof(count));
        freq[ch] = count;
    }

    // // Step 5: Rebuild the identical Huffman Tree
    HuffmanNode* root = buildHuffmanTree(freq);
    if (!root) {
        std::cerr << "[-] Error: Failed to reconstruct Huffman tree from header.\n";
        return false;
    }

    // // Step 6: Open output file for writing restored data
    std::ofstream outFile(outputPath, std::ios::binary);
    if (!outFile.is_open()) {
        std::cerr << "[-] Error: Cannot create restored output file: " << outputPath << "\n";
        delete root;
        return false;
    }

    // // Step 7: Decode bitstream by traversing the tree
    BitReader bitReader(inFile);
    uint64_t decodedBytes = 0;
    HuffmanNode* currentNode = root;

    // Buffer output writes for high performance
    const size_t OUT_BUFFER_SIZE = 4096;
    std::vector<char> outBuffer;
    outBuffer.reserve(OUT_BUFFER_SIZE);

    // Stop exactly when all original bytes have been restored
    // (This guarantees trailing padding bits in the final byte are ignored)
    while (decodedBytes < originalSize) {
        int bit = bitReader.readBit();
        if (bit == -1) {
            std::cerr << "[-] Error: Unexpected EOF in bitstream before decoding all bytes.\n";
            delete root;
            return false;
        }

        // Branch left on '0', right on '1'
        currentNode = (bit == 0) ? currentNode->left : currentNode->right;

        if (!currentNode) {
            std::cerr << "[-] Error: Invalid bit encountered during Huffman tree traversal.\n";
            delete root;
            return false;
        }

        // When a leaf node is reached, output its character
        if (currentNode->isLeaf()) {
            outBuffer.push_back(static_cast<char>(currentNode->character));
            decodedBytes++;

            // Flush buffer when full
            if (outBuffer.size() >= OUT_BUFFER_SIZE) {
                outFile.write(outBuffer.data(), outBuffer.size());
                outBuffer.clear();
            }

            // Reset tree pointer back to root for the next character
            currentNode = root;
        }
    }

    // Write any remaining buffered characters
    if (!outBuffer.empty()) {
        outFile.write(outBuffer.data(), outBuffer.size());
        outBuffer.clear();
    }

    // Close files and free tree memory
    inFile.close();
    outFile.close();
    delete root;

    std::cout << "\n========================================================================\n";
    std::cout << "                     DECOMPRESSION SUMMARY REPORT                       \n";
    std::cout << "========================================================================\n";
    std::cout << "  Input Encoded File:     " << inputPath << "\n";
    std::cout << "  Restored Output File:   " << outputPath << "\n";
    std::cout << "  Expected Bytes:         " << originalSize << " bytes\n";
    std::cout << "  Decoded Bytes:          " << decodedBytes << " bytes\n";
    std::cout << "  Status:                 Successfully restored identical file!\n";
    std::cout << "========================================================================\n\n";

    return true;
}

// ============================================================================
// SECTION 6: FILE INTEGRITY VERIFICATION (LOSSLESS PROOF)
// ============================================================================

// // Compares two files byte-by-byte to guarantee lossless reconstruction.
bool verifyFileIntegrity(const std::string& originalPath, const std::string& decompressedPath) {
    std::ifstream origFile(originalPath, std::ios::binary);
    std::ifstream decompFile(decompressedPath, std::ios::binary);

    if (!origFile.is_open()) {
        std::cerr << "[-] Error: Could not open original file: " << originalPath << "\n";
        return false;
    }
    if (!decompFile.is_open()) {
        std::cerr << "[-] Error: Could not open decompressed file: " << decompressedPath << "\n";
        return false;
    }

    // Compare file sizes first
    origFile.seekg(0, std::ios::end);
    decompFile.seekg(0, std::ios::end);
    std::streamsize origSize = origFile.tellg();
    std::streamsize decompSize = decompFile.tellg();

    std::cout << "\n========================================================================\n";
    std::cout << "                     FILE INTEGRITY VERIFICATION                        \n";
    std::cout << "========================================================================\n";
    std::cout << "  Original File:       " << originalPath << " (" << origSize << " bytes)\n";
    std::cout << "  Decompressed File:   " << decompressedPath << " (" << decompSize << " bytes)\n";

    if (origSize != decompSize) {
        std::cout << "  [FAIL] File sizes do NOT match! Lossless check failed.\n";
        std::cout << "========================================================================\n\n";
        return false;
    }

    // Rewind and compare byte-by-byte
    origFile.seekg(0, std::ios::beg);
    decompFile.seekg(0, std::ios::beg);

    char b1[4096], b2[4096];
    uint64_t byteOffset = 0;

    while (origFile.read(b1, sizeof(b1)) || origFile.gcount() > 0) {
        std::streamsize bytesRead = origFile.gcount();
        decompFile.read(b2, bytesRead);

        if (decompFile.gcount() != bytesRead) {
            std::cout << "  [FAIL] Stream mismatch at byte offset " << byteOffset << "!\n";
            std::cout << "========================================================================\n\n";
            return false;
        }

        for (std::streamsize i = 0; i < bytesRead; ++i) {
            if (b1[i] != b2[i]) {
                std::cout << "  [FAIL] Character mismatch at byte " << (byteOffset + i) << "!\n";
                std::cout << "         Original:     0x" << std::hex << static_cast<int>(static_cast<uint8_t>(b1[i])) << "\n";
                std::cout << "         Decompressed: 0x" << std::hex << static_cast<int>(static_cast<uint8_t>(b2[i])) << std::dec << "\n";
                std::cout << "========================================================================\n\n";
                return false;
            }
        }
        byteOffset += bytesRead;
    }

    std::cout << "  [PASS] 100% MATCH: Every single byte is identical!\n";
    std::cout << "         Lossless compression and decompression verified.\n";
    std::cout << "========================================================================\n\n";
    return true;
}

// ============================================================================
// SECTION 7: INTERACTIVE CONSOLE MENU & CLI DISPATCHER
// ============================================================================

void printBanner() {
    std::cout << "========================================================================\n";
    std::cout << "        HUFFMAN TEXT FILE COMPRESSION & DECOMPRESSION UTILITY          \n";
    std::cout << "             Lossless Data Compression in Modern C++17                 \n";
    std::cout << "========================================================================\n";
}

void printMenu() {
    std::cout << "\nChoose an operation:\n";
    std::cout << "  [1] Compress a Text File (.txt -> .huff)\n";
    std::cout << "  [2] Decompress a File   (.huff -> .txt)\n";
    std::cout << "  [3] Verify File Integrity (Compare Original vs Decompressed)\n";
    std::cout << "  [4] Exit Program\n";
    std::cout << "Enter your choice (1-4): ";
}

// Clean helper to read a line / path from console
std::string promptFilePath(const std::string& prompt) {
    std::cout << prompt;
    std::string path;
    std::getline(std::cin, path);
    // Remove surrounding quotes if user copied path with quotes
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
        path = path.substr(1, path.size() - 2);
    }
    return path;
}

int main(int argc, char* argv[]) {
    // // Support command-line arguments for automated testing / CLI workflows
    // Usage:
    //   huffman -c <input.txt> [output.huff]
    //   huffman -d <input.huff> [output.txt]
    //   huffman -v <original.txt> <decompressed.txt>
    if (argc >= 2) {
        std::string mode = argv[1];
        if (mode == "-c" && argc >= 3) {
            std::string inPath = argv[2];
            std::string outPath = (argc >= 4) ? argv[3] : (inPath + ".huff");
            return compressFile(inPath, outPath) ? 0 : 1;
        } else if (mode == "-d" && argc >= 3) {
            std::string inPath = argv[2];
            std::string outPath;
            if (argc >= 4) {
                outPath = argv[3];
            } else {
                bool endsWithHuff = (inPath.length() >= 5 && inPath.substr(inPath.length() - 5) == ".huff");
                outPath = endsWithHuff ? inPath.substr(0, inPath.length() - 5) + "_decompressed.txt"
                                       : inPath + ".out";
            }
            return decompressFile(inPath, outPath) ? 0 : 1;
        } else if (mode == "-v" && argc >= 4) {
            return verifyFileIntegrity(argv[2], argv[3]) ? 0 : 1;
        } else {
            std::cout << "Usage:\n";
            std::cout << "  Interactive Mode: " << argv[0] << "\n";
            std::cout << "  CLI Compression:  " << argv[0] << " -c <input.txt> [output.huff]\n";
            std::cout << "  CLI Decompression:" << argv[0] << " -d <input.huff> [output.txt]\n";
            std::cout << "  CLI Verification: " << argv[0] << " -v <original> <decompressed>\n";
            return 1;
        }
    }

    // // Interactive Console Mode
    printBanner();

    bool running = true;
    while (running) {
        printMenu();
        std::string choiceStr;
        if (!std::getline(std::cin, choiceStr)) {
            break;
        }

        if (choiceStr.empty()) continue;
        char choice = choiceStr[0];

        switch (choice) {
            case '1': {
                std::string inPath = promptFilePath("Enter path to input text file: ");
                if (inPath.empty() || !fs::exists(inPath)) {
                    std::cerr << "[-] Error: Input file does not exist.\n";
                    break;
                }
                std::string defaultOut = inPath + ".huff";
                std::cout << "Enter output compressed file path [default: " << defaultOut << "]: ";
                std::string outPath;
                std::getline(std::cin, outPath);
                if (outPath.empty()) {
                    outPath = defaultOut;
                }
                compressFile(inPath, outPath);
                break;
            }
            case '2': {
                std::string inPath = promptFilePath("Enter path to compressed .huff file: ");
                if (inPath.empty() || !fs::exists(inPath)) {
                    std::cerr << "[-] Error: File does not exist.\n";
                    break;
                }
                std::string defaultOut;
                if (inPath.size() >= 5 && inPath.substr(inPath.size() - 5) == ".huff") {
                    defaultOut = inPath.substr(0, inPath.size() - 5) + "_decompressed.txt";
                } else {
                    defaultOut = inPath + "_decompressed.txt";
                }
                std::cout << "Enter output decompressed file path [default: " << defaultOut << "]: ";
                std::string outPath;
                std::getline(std::cin, outPath);
                if (outPath.empty()) {
                    outPath = defaultOut;
                }
                decompressFile(inPath, outPath);
                break;
            }
            case '3': {
                std::string origPath = promptFilePath("Enter path to original file: ");
                std::string decompPath = promptFilePath("Enter path to decompressed file: ");
                verifyFileIntegrity(origPath, decompPath);
                break;
            }
            case '4': {
                std::cout << "\nExiting Huffman Utility. Have a great day!\n";
                running = false;
                break;
            }
            default: {
                std::cout << "[-] Invalid selection. Please enter 1, 2, 3, or 4.\n";
                break;
            }
        }
    }

    return 0;
}
