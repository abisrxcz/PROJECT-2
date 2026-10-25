#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <queue>
#include <unordered_map>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <algorithm>
#include <windows.h>

using namespace std;

//-> ANSI escape codes for colored console output
#define COLOR_RESET   "\033[0m" //-> Reset to default terminal color
#define COLOR_SKY_BLUE "\033[96m" //-> Bright Cyan / Sky Blue
#define COLOR_WHITE    "\033[97m" //-> Bright White
#define COLOR_GREEN    "\033[32m" //-> Green
#define COLOR_RED     "\033[31m" //-> Red

namespace fs {
inline bool exists(const string& path) {
    ifstream file(path, ios::binary);
    return file.good();
}

inline uint64_t file_size(const string& path) {
    ifstream file(path, ios::binary | ios::ate);
    if (!file) return 0;
    return static_cast<uint64_t>(file.tellg());
}
}

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

// // CompareNode is a functor used by priority_queue to build a Min-Heap.
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
    ofstream& out;  // Target binary output file stream
    uint8_t buffer;      // 8-bit accumulator holding bits until full
    int bitCount;        // Number of bits currently stored in the buffer (0 to 8)

public:
    explicit BitWriter(ofstream& stream)
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
    void writeBits(const string& bitString) {
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
    ifstream& in;   // Source binary input file stream
    uint8_t buffer;      // 8-bit byte currently being unpacked
    int bitPos;          // Next bit position to extract (7 down to 0, or -1 if empty)

public:
    explicit BitReader(ifstream& stream)
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
    priority_queue<HuffmanNode*, vector<HuffmanNode*>, CompareNode> pq;

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
        uint8_t combinedMinChar = min(leftChild->minChar, rightChild->minChar);

        HuffmanNode* parent = new HuffmanNode(combinedFreq, combinedMinChar, leftChild, rightChild);
        pq.push(parent);
    }

    // // The final remaining node is the root of the complete Huffman Tree
    return pq.top();
}

// // Traverses the Huffman Tree recursively to generate prefix-free binary codes.
// // Going left appends '0', going right appends '1'.
void generateHuffmanCodes(const HuffmanNode* node, const string& currentCode, string codes[256]) {
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
string formatChar(uint8_t ch) {
    switch (ch) {
        case '\n': return "'\\n' (Newline)";
        case '\r': return "'\\r' (Carriage Return)";
        case '\t': return "'\\t' (Tab)";
        case ' ':  return "' '  (Space)";
        default:
            if (ch >= 32 && ch <= 126) {
                return string("'") + static_cast<char>(ch) + "'";
            } else {
                return "0x" + ([ch]() {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "%02X", ch);
                    return string(buf);
                })();
            }
    }
}

// // Prints the Huffman Codebook table and frequency breakdown for analysis.
void displayCodeTable(const uint64_t freq[256], const string codes[256], uint64_t totalBytes) {
    cout << COLOR_SKY_BLUE << "\n================================================================================\n" << COLOR_RESET;
    cout << COLOR_WHITE << "                      COMPRESSION HUFFMAN TABLE                           \n" << COLOR_RESET;
    cout << COLOR_SKY_BLUE << "================================================================================\n" << COLOR_RESET;
    cout << COLOR_WHITE << left << setw(6)  << "Byte"
              << setw(24) << "Character"
              << setw(14) << "Frequency"
              << setw(12) << "Percentage"
              << setw(18) << "Huffman Code"
              << setw(8)  << "Length" << "\n"<< COLOR_RESET;
    cout << COLOR_SKY_BLUE << "--------------------------------------------------------------------------------\n" << COLOR_RESET;

    for (int i = 0; i < 256; ++i) {
        if (freq[i] > 0) {
            double pct = (totalBytes > 0) ? (static_cast<double>(freq[i]) / totalBytes * 100.0) : 0.0;
            std::ostringstream pctStream;
            pctStream << std::fixed << std::setprecision(2) << pct << "%";
            cout << left 
                      << COLOR_GREEN << setw(6) << i
                      << COLOR_SKY_BLUE << setw(24) <<  formatChar(static_cast<uint8_t>(i))
                      << COLOR_WHITE <<  setw(14) << freq[i]
                      << setw(14) << pctStream.str() 
                      << setw(18) << codes[i]
                      << setw(8)  << codes[i].length() << "\n" << COLOR_RESET;
        }
    }
    cout << COLOR_SKY_BLUE << "================================================================================\n\n" << COLOR_RESET;
}
//=========================================================================================

//SECTION 3-B: DECOMPRESSION TABLE DISPLAY

//==============================================================================================
// // Prints the Huffman Codebook table for Decompression based on reconstructed metadata.
void displayDecompressionTable(const uint64_t freq[256], const string codes[256], uint64_t totalBytes) {
    cout << COLOR_SKY_BLUE << "\n================================================================================\n" << COLOR_RESET;
    cout << COLOR_WHITE << "                   DECOMPRESSION HUFFMAN TABLE                       \n" << COLOR_RESET;
    cout << COLOR_SKY_BLUE << "================================================================================\n" << COLOR_RESET;
    cout << COLOR_WHITE << left << setw(6)  << "Byte"
              << setw(24) << "Character"
              << setw(14) << "Restored Freq"
              << setw(12) << "Percentage"
              << setw(18) << "Decoded Code"
              << setw(8)  << "Length" << COLOR_RESET << "\n";
    cout << COLOR_SKY_BLUE << "--------------------------------------------------------------------------------\n" << COLOR_RESET;

    for (int i = 0; i < 256; ++i) {
        if (freq[i] > 0) {
            double pct = (totalBytes > 0) ? (static_cast<double>(freq[i]) / totalBytes * 100.0) : 0.0;
            std::ostringstream pctStream;
        pctStream << std::fixed << std::setprecision(2) << pct << "%";
            cout << left 
                      << COLOR_GREEN << setw(6) << i
                      << COLOR_SKY_BLUE << setw(24) <<  formatChar(static_cast<uint8_t>(i))
                      << COLOR_WHITE << setw(14) <<  freq[i]
                      << setw(14) << pctStream.str() 
                      << setw(18) << codes[i]
                      << setw(8)  << codes[i].length() << "\n" << COLOR_RESET;
        }
    }
    cout << COLOR_SKY_BLUE << "================================================================================\n\n" << COLOR_RESET;
}


// ============================================================================
// SECTION 4: FILE COMPRESSION
// ============================================================================

// // Compresses an input text file into a binary .huff file.
// // Encapsulates metadata (original file size, unique character frequency table)
// // alongside the bit-packed payload.
bool compressFile(const string& inputPath, const string& outputPath) {
    // // Step 1: Open source file in binary mode
    ifstream inFile(inputPath, ios::binary);
    if (!inFile.is_open()) {
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        cerr << COLOR_RED << "[!]" <<COLOR_WHITE<< " Error: Failed to open input file: " << inputPath << "\n";
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        return false;
    }

    // // Step 2: Character Frequency Analysis
    // Count exact occurrences of each byte across the entire file
    uint64_t freq[256] = {0};
    uint64_t originalSize = 0;
    char buffer[4096];

    while (inFile.read(buffer, sizeof(buffer)) || inFile.gcount() > 0) {
        streamsize bytesRead = inFile.gcount();
        originalSize += bytesRead;
        for (streamsize i = 0; i < bytesRead; ++i) {
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
    cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
    cout << COLOR_GREEN << "[+]" << COLOR_WHITE << " Character frequency analysis completed.\n";
    cout << COLOR_GREEN <<"    Original File Size: "<< COLOR_WHITE << originalSize << " bytes\n";
    cout << COLOR_GREEN <<"    Unique Characters:  "<< COLOR_WHITE << uniqueCharCount << "\n\n";

    // // Step 3: Handle empty file edge case
    if (originalSize == 0) {
        ofstream outFile(outputPath, ios::binary);
        if (!outFile.is_open()) {
            cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
            cerr << COLOR_RED << "[!]" << COLOR_WHITE << " Error: Cannot create output file: " << outputPath << "\n\n";
            cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
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
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        cout << COLOR_RED << "[!]" << COLOR_WHITE << " Input file is empty. Generated empty compressed archive.\n" << COLOR_RESET;
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        return true;
    }
    //->loading bar for compression
    int totalWidth = 30;
    cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
    cout << COLOR_WHITE << "Building Huffman tree & compressing file...\n\n" << COLOR_RESET;

    for (int i = 0; i <= 100; i += 2) {
        int filledWidth = (i * totalWidth) / 100;

        cout << "\r[";

        cout << COLOR_GREEN;
        for (int j = 0; j < filledWidth; ++j) {
            cout << "=";
        }

        cout << COLOR_WHITE;
        for (int j = filledWidth; j < totalWidth; ++j) {
            cout << " ";
        }

        cout << "] " << COLOR_GREEN << i << "%" << COLOR_WHITE << flush;
        Sleep(15);
    }

    cout << "\n\n";

    // // Step 4: Build Huffman Tree from frequency counts
    HuffmanNode* root = buildHuffmanTree(freq);

    // // Step 5: Generate prefix-free variable-length binary codes
    string codes[256];
    generateHuffmanCodes(root, "", codes);

    // Display the codebook table for review
    displayCodeTable(freq, codes, originalSize);

    // // Step 6: Prepare output file and write header metadata
    ofstream outFile(outputPath, ios::binary);
    if (!outFile.is_open()) {
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        cerr << COLOR_RED << "[!]" << COLOR_WHITE << " Error: Cannot create output file: " << outputPath << "\n\n";
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
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
    inFile.seekg(0, ios::beg); // Rewind to file beginning

    BitWriter bitWriter(outFile);
    while (inFile.read(buffer, sizeof(buffer)) || inFile.gcount() > 0) {
        streamsize bytesRead = inFile.gcount();
        for (streamsize i = 0; i < bytesRead; ++i) {
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

    cout << COLOR_SKY_BLUE << "\n========================================================================\n\n" << COLOR_RESET;
    cout << COLOR_WHITE << "                      COMPRESSION SUMMARY REPORT                        \n\n";
    cout << COLOR_SKY_BLUE << "========================================================================\n" << COLOR_RESET;
    cout << COLOR_GREEN << "  Original File:          " << COLOR_WHITE << inputPath << "\n";
    cout << COLOR_GREEN << "  Compressed File:        " << COLOR_WHITE << outputPath << "\n";
    cout << COLOR_GREEN << "  Original File Size:     " << COLOR_WHITE << originalSize << " bytes\n";
    cout << COLOR_GREEN << "  Compressed File Size:   " << COLOR_WHITE << compressedSize << " bytes\n";
    cout << COLOR_GREEN << "  Space Saved:            " << COLOR_WHITE << fixed << setprecision(2) << spaceSaved << " %\n";
    cout << COLOR_GREEN << "  Compression Ratio:      " << COLOR_WHITE << fixed << setprecision(2)
              << (compressedSize > 0 ? (static_cast<double>(originalSize) / compressedSize) : 0.0) << " : 1\n";
    cout << COLOR_SKY_BLUE << "========================================================================\n\n" << COLOR_RESET;

    return true;
}

// ============================================================================
// SECTION 5: FILE DECOMPRESSION
// ============================================================================

// // Decompresses an encoded .huff file back into the exact original text.
// // Reads header metadata, reconstructs the identical Huffman tree,
// // and traverses the tree bit-by-bit to decode original characters.
bool decompressFile(const string& inputPath, const string& outputPath) {
    // // Step 1: Open compressed file in binary mode
    ifstream inFile(inputPath, ios::binary);
    if (!inFile.is_open()) {
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        cerr << COLOR_RED<< "[!]"<< COLOR_WHITE << " Error: Failed to open compressed file: " << inputPath << "\n\n" << COLOR_RESET;
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        return false;
    }

    // // Step 2: Validate Header Magic Identifier
    char magic[4];
    if (!inFile.read(magic, 4) || magic[0] != 'H' || magic[1] != 'U' || magic[2] != 'F' || magic[3] != '1') {
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        cerr << COLOR_RED<< "[!]"<< COLOR_WHITE << " Error: Invalid file format or corrupted header. Expected 'HUF1'.\n\n" << COLOR_RESET;
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        return false;
    }

    // // Step 3: Read original file size and unique character count
    uint64_t originalSize = 0;
    uint16_t uniqueCharCount = 0;
    inFile.read(reinterpret_cast<char*>(&originalSize), sizeof(originalSize));
    inFile.read(reinterpret_cast<char*>(&uniqueCharCount), sizeof(uniqueCharCount));

    // Handle empty file edge case
    if (originalSize == 0) {
        ofstream outFile(outputPath, ios::binary);
        outFile.close();
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        cout << COLOR_GREEN << "[+]" << COLOR_WHITE << " Decompression completed: original file was empty (0 bytes).\n\n" << COLOR_RESET;
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
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
     //->loading bar for compression
    int totalWidth = 30;
    cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
    cout << COLOR_WHITE << "Rebuilding Huffman tree & decompressing file...\n\n" << COLOR_RESET;

    for (int i = 0; i <= 100; i += 2) {
        int filledWidth = (i * totalWidth) / 100;

        cout << "\r[";

        cout << COLOR_GREEN;
        for (int j = 0; j < filledWidth; ++j) {
            cout << "=";
        }

        cout << COLOR_WHITE;
        for (int j = filledWidth; j < totalWidth; ++j) {
            cout << " ";
        }

        cout << "] " << COLOR_GREEN << i << "%" << COLOR_WHITE << flush;
        Sleep(15);
    }

    cout << "\n\n";

    // // Step 5: Rebuild the identical Huffman Tree
    HuffmanNode* root = buildHuffmanTree(freq);
    if (!root) {
        cerr << COLOR_RED << "[!]" << COLOR_WHITE << " Error: Failed to reconstruct Huffman tree from header.\n" << COLOR_RESET;
        return false;
    }

    // // Generate Huffman codes from the reconstructed tree
    string codes[256];
    generateHuffmanCodes(root, "", codes);

    // // Display the restored Codebook Table
    displayDecompressionTable(freq, codes, originalSize);

    // // Step 6: Open output file for writing restored data
    ofstream outFile(outputPath, ios::binary);
    if (!outFile.is_open()) {
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        cerr << COLOR_RED<< "[!]"<< COLOR_WHITE << " Error: Cannot create restored output file: " << outputPath << "\n\n" << COLOR_RESET;
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        delete root;
        return false;
    }

    // // Step 7: Decode bitstream by traversing the tree
    BitReader bitReader(inFile);
    uint64_t decodedBytes = 0;
    HuffmanNode* currentNode = root;

    // Buffer output writes for high performance
    const size_t OUT_BUFFER_SIZE = 4096;
    vector<char> outBuffer;
    outBuffer.reserve(OUT_BUFFER_SIZE);

    // Stop exactly when all original bytes have been restored
    // (This guarantees trailing padding bits in the final byte are ignored)
    while (decodedBytes < originalSize) {
        int bit = bitReader.readBit();
        if (bit == -1) {
            cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
            cerr << COLOR_RED<< "[!]"<< COLOR_WHITE << " Error: Unexpected EOF in bitstream before decoding all bytes.\n\n" << COLOR_RESET;
            cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
            delete root;
            return false;
        }

        // Branch left on '0', right on '1'
        currentNode = (bit == 0) ? currentNode->left : currentNode->right;

        if (!currentNode) {
            cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
            cerr << COLOR_RED<< "[!]"<< COLOR_WHITE << " Error: Invalid bit encountered during Huffman tree traversal.\n\n" << COLOR_RESET;
            cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
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

    cout << COLOR_SKY_BLUE << "\n========================================================================\n" << COLOR_RESET;
    cout << COLOR_WHITE << "                     DECOMPRESSION SUMMARY REPORT                       \n\n" << COLOR_RESET;
    cout << COLOR_SKY_BLUE << "========================================================================\n" << COLOR_RESET;
    cout << COLOR_GREEN << "  Input Encoded File:     " << COLOR_WHITE << inputPath << "\n" << COLOR_RESET;
    cout << COLOR_GREEN << "  Restored Output File:   " << COLOR_WHITE << outputPath << "\n" << COLOR_RESET;
    cout << COLOR_GREEN << "  Expected Bytes:         " << COLOR_WHITE << originalSize << " bytes\n" << COLOR_RESET;
    cout << COLOR_GREEN << "  Decoded Bytes:          " << COLOR_WHITE << decodedBytes << " bytes\n" << COLOR_RESET;
    cout << COLOR_GREEN << "  Status:                "  << COLOR_WHITE << "Successfully restored identical file!\n" << COLOR_RESET;
    cout << COLOR_SKY_BLUE << "========================================================================\n\n" << COLOR_RESET;

    return true;
}

// ============================================================================
// SECTION 6: FILE INTEGRITY VERIFICATION (LOSSLESS PROOF)
// ============================================================================

// // Compares two files byte-by-byte to guarantee lossless reconstruction.
bool verifyFileIntegrity(const string& originalPath, const string& decompressedPath) {
    ifstream origFile(originalPath, ios::binary);
    ifstream decompFile(decompressedPath, ios::binary);

    if (!origFile.is_open()) {
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        cerr << COLOR_RED <<"[!]" << COLOR_WHITE << " Error: Could not open original file: " << originalPath << "\n\n" << COLOR_RESET;
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        return false;
    }
    if (!decompFile.is_open()) {
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        cerr << COLOR_RED <<"[!]" << COLOR_WHITE << " Error: Could not open decompressed file: " << decompressedPath << "\n\n" << COLOR_RESET;
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        return false;
    }
    //loading bar for verification
    int totalWidth = 30;
    cout << COLOR_SKY_BLUE << "\n-------------------------------------------------------------------------\n\n" << COLOR_RESET;
    cout << COLOR_WHITE << "Reading and verifying byte streams...\n  " << COLOR_RESET;

    for (int i = 0; i <= 100; i += 2) {
        int filledWidth = (i * totalWidth) / 100;

        cout << "\r[";

        cout << COLOR_GREEN;
        for (int j = 0; j < filledWidth; ++j) {
            cout << "=";
        }

        cout << COLOR_WHITE;
        for (int j = filledWidth; j < totalWidth; ++j) {
            cout << " ";
        }

        cout << "] " << COLOR_GREEN << i << "%" << COLOR_WHITE << flush;
        Sleep(15);
    }

    cout << "\n\n";

    // Compare file sizes first
    origFile.seekg(0, ios::end);
    decompFile.seekg(0, ios::end);
    streamsize origSize = origFile.tellg();
    streamsize decompSize = decompFile.tellg();
    

    cout << COLOR_SKY_BLUE << "\n========================================================================\n\n" << COLOR_RESET;

    cout << COLOR_WHITE << "                       FILE INTEGRITY VERIFICATION                        \n" << COLOR_RESET;
    cout << COLOR_GREEN << "      Decompression check: " << COLOR_WHITE << "Comparing original and decompressed files           \n\n" << COLOR_RESET;

    cout << COLOR_SKY_BLUE << "========================================================================   \n" << COLOR_RESET;
    cout << COLOR_GREEN << "  Decompressed File:   " << COLOR_WHITE << decompressedPath << " (" << decompSize << " bytes)\n " << COLOR_RESET;
    cout << COLOR_GREEN << "  Original File:       " << COLOR_WHITE << originalPath << " (" << origSize << " bytes)\n" << COLOR_RESET;

    if (origSize != decompSize) {
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        cout << COLOR_RED << "  [ERROR]" << COLOR_WHITE << " File sizes do NOT match! Lossless check failed.\n\n" << COLOR_RESET;
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        return false;
    }

    // Rewind and compare byte-by-byte
    origFile.seekg(0, ios::beg);
    decompFile.seekg(0, ios::beg);

    char b1[4096], b2[4096];
    uint64_t byteOffset = 0;

    while (origFile.read(b1, sizeof(b1)) || origFile.gcount() > 0) {
        streamsize bytesRead = origFile.gcount();
        decompFile.read(b2, bytesRead);

        if (decompFile.gcount() != bytesRead) {
            cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
            cout << COLOR_RED << "  [ERROR]" << COLOR_WHITE << " Stream mismatch at byte offset " << byteOffset << "!\n\n" << COLOR_RESET;
            cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
            return false;
        }

        for (streamsize i = 0; i < bytesRead; ++i) {
            if (b1[i] != b2[i]) {
                cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
                cout << COLOR_RED << "  [ERROR]" << COLOR_WHITE << " Character mismatch at byte " << (byteOffset + i) << "!\n" << COLOR_RESET;
                cout << COLOR_GREEN << "         Original:     0x" << COLOR_WHITE << hex << static_cast<int>(static_cast<uint8_t>(b1[i])) << "\n";
                cout << COLOR_GREEN << "         Decompressed: 0x" << COLOR_WHITE << hex << static_cast<int>(static_cast<uint8_t>(b2[i])) << dec << "\n";
                cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
                return false;
            }
        }
        byteOffset += bytesRead;
    }

    cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
    cout << COLOR_GREEN << "  [PASS]" << COLOR_WHITE << " 100% MATCH: Every single byte is identical!\n" << COLOR_RESET;
    cout << "\n";
    cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
    return true;
}

// ============================================================================
// SECTION 7: INTERACTIVE CONSOLE MENU & CLI DISPATCHER
// ============================================================================

void printBanner() {
    cout << COLOR_SKY_BLUE << "=========================================================================\n" << COLOR_RESET;
    cout << "\n";
    cout << COLOR_WHITE << "   TEXT FILE COMPRESSION AND DECOMPRESSION USING HUFFMAN CODING IN C++\n" << COLOR_RESET;
    cout << COLOR_GREEN << "        Programmed by: "<<COLOR_WHITE<<"Vherleen Kail Aday and Abby Shane Daseco\n" << COLOR_RESET;
    cout << "\n";
    cout << COLOR_SKY_BLUE << "=========================================================================\n" << COLOR_RESET;
    cout << "\n";
}

void printMenu() {
    cout << COLOR_SKY_BLUE << "Choose an operation:\n\n" << COLOR_RESET;
    
    cout << COLOR_GREEN << "[1]" << COLOR_WHITE << " Compress a Text File "<<COLOR_GREEN<<"(.txt -> .huff)\n" << COLOR_RESET;
    cout << COLOR_GREEN << "[2]" << COLOR_WHITE << " Decompress a File   "<<COLOR_GREEN<<"(.huff -> .txt)\n" << COLOR_RESET;
    cout << COLOR_GREEN << "[3]" << COLOR_WHITE << " Verify File Integrity "<<COLOR_GREEN<<"(Compare Original vs Decompressed)\n" << COLOR_RESET;
    cout << COLOR_GREEN << "[4]" << COLOR_WHITE << " Exit Program\n\n" << COLOR_RESET;
    cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
    cout << "\n" << COLOR_WHITE << "Enter your choice "<<COLOR_GREEN<<"(1-4) -> " << COLOR_RESET;
}

// Clean helper to read a line / path from console
string promptFilePath(const string& prompt) {
    cout << prompt;
    string path;
    getline(cin, path);
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
        string mode = argv[1];
        if (mode == "-c" && argc >= 3) {
            string inPath = argv[2];
            string outPath = (argc >= 4) ? argv[3] : (inPath + ".huff");
            return compressFile(inPath, outPath) ? 0 : 1;
        } else if (mode == "-d" && argc >= 3) {
            string inPath = argv[2];
            string outPath;
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
            cout << COLOR_GREEN << "Usage:\n" << COLOR_RESET;
            cout << COLOR_GREEN << "  Interactive Mode: " << COLOR_WHITE << argv[0] << "\n" << COLOR_RESET;
            cout << COLOR_GREEN << "  CLI Compression:  " << COLOR_WHITE << argv[0] << " -c <input.txt> [output.huff]\n" << COLOR_RESET;
            cout << COLOR_GREEN << "  CLI Decompression:" << COLOR_WHITE << argv[0] << " -d <input.huff> [output.txt]\n" << COLOR_RESET;
            cout << COLOR_GREEN << "  CLI Verification: " << COLOR_WHITE << argv[0] << " -v <original> <decompressed>\n" << COLOR_RESET;
            return 1;
        }
    }

    // // Interactive Console Mode
    printBanner();

    bool running = true;
    while (running) {
        printMenu();
        string choiceStr;
        if (!getline(cin, choiceStr)) {
            break;
        }

        if (choiceStr.empty()) continue;
        char choice = choiceStr[0];

        switch (choice) {
            case '1': {
                string inPath = promptFilePath("Enter path to input text file -> ");
                if (inPath.empty() || !fs::exists(inPath)) {
                    cerr << COLOR_RED << "[!]" << COLOR_WHITE <<  "Error: Input file does not exist.\n" << COLOR_RESET;
                    break;
                }
                string defaultOut = inPath + ".huff";
                cout << "Please click enter for the output file path [default: " << defaultOut << "]-> ";
                string outPath;
                getline(cin, outPath);
                if (outPath.empty()) {
                    outPath = defaultOut;
                }
                compressFile(inPath, outPath);
                break;
            }
            case '2': {
                string inPath = promptFilePath("Enter path to compressed .huff file -> ");
                if (inPath.empty() || !fs::exists(inPath)) {
                    cerr << COLOR_RED << "[!]" << COLOR_WHITE << "Error: File does not exist.\n" << COLOR_RESET;
                    break;
                }
                string defaultOut;
                if (inPath.size() >= 5 && inPath.substr(inPath.size() - 5) == ".huff") {
                    defaultOut = inPath.substr(0, inPath.size() - 5) + "_decompressed.txt";
                } else {
                    defaultOut = inPath + "_decompressed.txt";
                }
                cout << "Please click enter for the output file path [default: " << defaultOut << "]-> ";
                string outPath;
                getline(cin, outPath);
                if (outPath.empty()) {
                    outPath = defaultOut;
                }
                decompressFile(inPath, outPath);
                break;
            }
            case '3': {
                string origPath = promptFilePath("Enter path to original file -> ");
                string decompPath = promptFilePath("Enter path to decompressed file -> ");
                verifyFileIntegrity(origPath, decompPath);
                break;
            }
            case '4': {
                cout << COLOR_GREEN << "\nExiting Huffman Utility. Thank you for using our program!\n" << COLOR_RESET;
                running = false;
                break;
            }
            default: {
                cout << COLOR_RED << "[!]" << COLOR_WHITE << " Invalid choice. Please enter 1, 2, 3, or 4.\n" << COLOR_RESET;
                break;
            }
        }
    }

    return 0;
}
