//DSA - PROJECT 2: FILE COMPRESSION AND DECOMPRESSION USING HUFFMAN CODING
//Programmed by: Vherleen Kail Aday and Abby Shane Daseco

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



// -> SECTION 1: HUFFMAN TREE DATA STRUCTURES



//-> Creating a node that represents an individual node in the binary Huffman Tree.
struct HuffmanNode {
    uint8_t character;       //-> Byte value
    uint64_t frequency;      //-> number of character
    uint8_t minChar;         //-> Smallest character in this subtree
    HuffmanNode* left;       //-> Pointer to left child -> represents bit '0'
    HuffmanNode* right;      //-> Pointer to right child -> represents bit '1'

    //-> Constructor for leaf nodes: stores charac and frequency
    HuffmanNode(uint8_t ch, uint64_t freq)
        : character(ch), frequency(freq), minChar(ch), left(nullptr), right(nullptr) {}

    //->Constructor for internal nodes: combines two child subtrees with their sum of frequencies
    HuffmanNode(uint64_t freq, uint8_t minC, HuffmanNode* l, HuffmanNode* r)
        : character(0), frequency(freq), minChar(minC), left(l), right(r) {}

    //-> Destructor to automatically clean up tree memory and prevent leaks.
    ~HuffmanNode() {
        delete left;
        delete right;
    }

    //-> Helper method to check if the node is a leaf -> means it represents an actual character rather than an internal node.
    bool isLeaf() const {
        return left == nullptr && right == nullptr;
    }
};

//-> Node comparison for priority queue: lower frequency has higher priority, and tie-breaker is minChar.
struct CompareNode {
    bool operator()(const HuffmanNode* a, const HuffmanNode* b) const {
        //-> Primary criteria: frequency -> lower frequency has higher priority
        if (a->frequency != b->frequency) {
            return a->frequency > b->frequency;
        }
        //-> Secondary criteria: minChar -> lower character value has higher priority
        return a->minChar > b->minChar;
    }
};


// -> SECTION 2: BIT-LEVEL I/O STREAMS (BIT PACKING & UNPACKING)


// -> this class handles bit-level writing to a binary output stream -> it accumulates bits into bytes and writes them to disk
class BitWriter {
private:
    ofstream& out;  //-> Target binary output file stream
    uint8_t buffer;      //-> 8-bit accumulator holding bits until full
    int bitCount;        //-> Number of bits currently stored in the buffer -> 0 to 8

public:
    explicit BitWriter(ofstream& stream)
        : out(stream), buffer(0), bitCount(0) {}

    //-> This function writes a single bit -> 0 or 1 -> into the byte accumulator.
    void writeBit(int bit) {
        //-> Shift existing bits to the left and insert the new bit at the least significant position
        buffer = (buffer << 1) | (bit & 1);
        bitCount++;

        //-> When 8 bits are accumulated, write the full byte to disk and reset
        if (bitCount == 8) {
            out.put(static_cast<char>(buffer));
            buffer = 0;
            bitCount = 0;
        }
    }

    //-> This function iterates through a binary string and writes each bit
    void writeBits(const string& bitString) {
        for (char c : bitString) {
            writeBit(c == '1' ? 1 : 0);
        }
    }

    //-> This function flushes any remaining bits in the buffer to disk, padding with zeros if necessary
    void flush() {
        if (bitCount > 0) {
            buffer <<= (8 - bitCount); // Pad remaining bits with zeros on the right
            out.put(static_cast<char>(buffer));
            buffer = 0;
            bitCount = 0;
        }
    }
};

// -> This class handles bit-level reading from a binary input stream -> it unpacks bits from bytes read from disk
class BitReader {
private:
    ifstream& in;   //-> Source binary input file stream
    uint8_t buffer;      //-> 8-bit byte currently being unpacked
    int bitPos;          //-> Next bit position to extract (7 down to 0, or -1 if empty)

public:
    explicit BitReader(ifstream& stream)
        : in(stream), buffer(0), bitPos(-1) {}

    //-> This function reads the next bit from the stream
    int readBit() {
        //-> If all bits in the current byte have been read, fetch the next byte from the stream
        if (bitPos < 0) {
            int nextByte = in.get();
            if (nextByte == EOF) {
                return -1; 
            }
            buffer = static_cast<uint8_t>(nextByte);
            bitPos = 7; // -> Resetting to the most significant bit (MSB)
        }

        //-> Extract the bit at bitPos (from Most Significant Bit to Least Significant Bit) and decrement position
        int bit = (buffer >> bitPos) & 1;
        bitPos--;
        return bit;
    }
};


// ->SECTION 3: HUFFMAN ALGORITHM CORE LOGIC


//-> This function builds the Huffman Tree from an array of 256 character frequencies.
HuffmanNode* buildHuffmanTree(const uint64_t freq[256]) {
    //-> [Initialize min-priority queue]
    priority_queue<HuffmanNode*, vector<HuffmanNode*>, CompareNode> pq;

    for (int i = 0; i < 256; ++i) {
        if (freq[i] > 0) {
            pq.push(new HuffmanNode(static_cast<uint8_t>(i), freq[i]));
        }
    }

    //-> Handle edge case: empty file (no characters)
    if (pq.empty()) {
        return nullptr;
    }

    //-> Handle edge case: file contains only 1 unique character
    if (pq.size() == 1) {
        HuffmanNode* onlyLeaf = pq.top(); //-> Get the single character node
        pq.pop();
        HuffmanNode* root = new HuffmanNode(onlyLeaf->frequency, onlyLeaf->minChar, onlyLeaf, nullptr); //-> Create a dummy parent node for the single character
        return root; //-> Returning the root of the tree
    }

    //-> [Iteratively combine the two lowest-frequency subtrees]
    while (pq.size() > 1) {
        //-> Extracting the two lowest frequency nodes
        HuffmanNode* leftChild = pq.top(); //-> Left child represents bit '0'
        pq.pop();
        HuffmanNode* rightChild = pq.top();//-> Right child represents bit '1'
        pq.pop();

        //-> Creating an internal parent node with combined frequency
        uint64_t combinedFreq = leftChild->frequency + rightChild->frequency; //-> Sum of frequencies of the two children
        uint8_t combinedMinChar = min(leftChild->minChar, rightChild->minChar); //-> Minimum character value among the two children for tie-breaking

        HuffmanNode* parent = new HuffmanNode(combinedFreq, combinedMinChar, leftChild, rightChild); //-> Create a new internal node with left and right children
        pq.push(parent); //-> Push the new parent node back into the priority queue for further processing
    }

    return pq.top(); //-> The last remaining node in the priority queue is the root of the complete Huffman Tree
}

// -> This function recursively traverses the Huffman Tree to generate prefix-free binary codes for each character
void generateHuffmanCodes(const HuffmanNode* node, const string& currentCode, string codes[256]) {
    if (!node) return;

    //-> If the current node is a leaf, assign the accumulated binary code to the corresponding character in the codes array
    if (node->isLeaf()) {
        //-> If the current code is empty (which can happen if there's only one unique character), assign "0" as its code to ensure a valid representation
        codes[node->character] = currentCode.empty() ? "0" : currentCode;
        return;
    }

    //-> Traverse left branch (bit 0)
    generateHuffmanCodes(node->left, currentCode + "0", codes);

    //-> Traverse right branch (bit 1)
    generateHuffmanCodes(node->right, currentCode + "1", codes);
}

// ->This Helper function is used to print an ASCII / character representation cleanly for students
string formatChar(uint8_t ch) {
    switch (ch) {
        case '\n': return "'\\n' (Newline)"; //-> Special case for newline character
        case '\r': return "'\\r' (Carriage Return)"; //-> Special case for carriage return character
        case '\t': return "'\\t' (Tab)"; //-> Special case for tab character
        case ' ':  return "' '  (Space)"; //-> Special case for space character
        default:
            if (ch >= 32 && ch <= 126) { //-> Printable ASCII range
                return string("'") + static_cast<char>(ch) + "'"; //-> Return the character itself for printable ASCII
            } else {
                return "0x" + ([ch]() { //-> Return hexadecimal representation for non-printable characters
                    char buf[8];
                    snprintf(buf, sizeof(buf), "%02X", ch); //-> Format the character as a two-digit hexadecimal string
                    return string(buf); //-> Return the formatted string
                })();
            }
    }
}

//_> This function prints the Compression Huffman table with character frequencies, percentages, and their corresponding binary codes.
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

//-this function prints the Decompression Huffman table with restored character frequencies, percentages, and their corresponding binary codes.
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



// -> SECTION 4: FILE COMPRESSION


//-> This bool function compresses a given input file using Huffman coding and writes the compressed data to an output file
bool compressFile(const string& inputPath, const string& outputPath) {
    //-> [Opening source file in binary mode]
    ifstream inFile(inputPath, ios::binary);
    if (!inFile.is_open()) { //-> Check if the input file was successfully opened
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        cerr << COLOR_RED << "[!]" <<COLOR_WHITE<< " Error: Failed to open input file: " << inputPath << "\n";
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        return false;
    }

    //-> [Character Frequency Analysis]
    uint64_t freq[256] = {0}; //-> Array to hold frequency counts for each possible byte value (0-255)
    uint64_t originalSize = 0; //-> Variable to track the total number of bytes read from the input file
    char buffer[4096]; //-> Buffer to read chunks of the input file for efficient processing

    while (inFile.read(buffer, sizeof(buffer)) || inFile.gcount() > 0) { //-> Read the file in chunks until no more data is available
        streamsize bytesRead = inFile.gcount(); //-> Get the actual number of bytes read in this iteration
        originalSize += bytesRead; //-> Accumulate the total size of the original file
        for (streamsize i = 0; i < bytesRead; ++i) { //-> Iterate through each byte in the buffer and update its frequency count
            freq[static_cast<uint8_t>(buffer[i])]++; //-> Increment the frequency count for the corresponding byte value
        }
    }

    //-> This count how many unique characters appeared in the file
    uint16_t uniqueCharCount = 0;
    for (int i = 0; i < 256; ++i) { //-> Iterate through the frequency array to count how many unique characters were found
        if (freq[i] > 0) {
            uniqueCharCount++;
        }
    }
    cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
    cout << COLOR_GREEN << "[+]" << COLOR_WHITE << " Character frequency analysis completed.\n";
    cout << COLOR_GREEN <<"    Original File Size: "<< COLOR_WHITE << originalSize << " bytes\n"; //-> Display the total size of the original file
    cout << COLOR_GREEN <<"    Unique Characters:  "<< COLOR_WHITE << uniqueCharCount << "\n\n"; //->  Display the number of unique characters found in the file

    //->[Handle empty file edge case]
    if (originalSize == 0) {
        ofstream outFile(outputPath, ios::binary);
        if (!outFile.is_open()) {
            cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
            cerr << COLOR_RED << "[!]" << COLOR_WHITE << " Error: Cannot create output file: " << outputPath << "\n\n";
            cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
            return false;
        }
        //-> this writes the header for an empty compressed file, including the magic identifier and zeroed metadata
        const char magic[4] = {'H', 'U', 'F', '1'};
        outFile.write(magic, 4);
        uint64_t zeroSize = 0;
        uint16_t zeroUniq = 0;
        outFile.write(reinterpret_cast<const char*>(&zeroSize), sizeof(zeroSize)); //-> Write original file size (0 bytes)
        outFile.write(reinterpret_cast<const char*>(&zeroUniq), sizeof(zeroUniq)); //-> Write unique character count (0)
        outFile.close();
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        cout << COLOR_RED << "[!]" << COLOR_WHITE << " Input file is empty. Generated empty compressed archive.\n" << COLOR_RESET;
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        return true;
    }
    //-> Display a progress bar for building the Huffman tree and compressing the file
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

    //-> [Build Huffman Tree from frequency counts]
    HuffmanNode* root = buildHuffmanTree(freq);

    //->[Generate prefix-free variable-length binary codes]
    string codes[256];
    generateHuffmanCodes(root, "", codes);

    //-> Display the codetable for review
    displayCodeTable(freq, codes, originalSize);

    //->[Prepare output file and write header metadata]
    ofstream outFile(outputPath, ios::binary);
    if (!outFile.is_open()) {
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        cerr << COLOR_RED << "[!]" << COLOR_WHITE << " Error: Cannot create output file: " << outputPath << "\n\n";
        cout << COLOR_SKY_BLUE << "-------------------------------------------------------------------------\n\n" << COLOR_RESET;
        delete root;
        return false;
    }

    //-> const char magic for file identification
    const char magic[4] = {'H', 'U', 'F', '1'};
    outFile.write(magic, 4); //-> Write the magic identifier to the output file
    outFile.write(reinterpret_cast<const char*>(&originalSize), sizeof(originalSize)); //-> Write the original file size to the output file
    outFile.write(reinterpret_cast<const char*>(&uniqueCharCount), sizeof(uniqueCharCount)); //-> Write the number of unique characters to the output file

    for (int i = 0; i < 256; ++i) { //-> Write the frequency table for each unique character to the output file
        if (freq[i] > 0) {
            uint8_t ch = static_cast<uint8_t>(i);
            uint64_t count = freq[i];
            outFile.write(reinterpret_cast<const char*>(&ch), sizeof(ch)); //-> Write the character byte
            outFile.write(reinterpret_cast<const char*>(&count), sizeof(count)); //-> Write the frequency count for that character
        }
    }

    //->[Rewind input file and encode content into bitstream]
    inFile.clear(); //-> Clear any error from the input file stream
    inFile.seekg(0, ios::beg); //-> Rewind the input file to the beginning for reading again

    BitWriter bitWriter(outFile); //-> Create a BitWriter to handle bit-level writing to the output file
    while (inFile.read(buffer, sizeof(buffer)) || inFile.gcount() > 0) { //-> Read the input file in chunks again for encoding
        streamsize bytesRead = inFile.gcount();
        for (streamsize i = 0; i < bytesRead; ++i) { //-> For each byte read, look up its Huffman code and write the corresponding bits to the output file
            uint8_t ch = static_cast<uint8_t>(buffer[i]);
            bitWriter.writeBits(codes[ch]);
        }
    }

    //-> Flush any remaining partial byte
    bitWriter.flush();

    inFile.close(); //-> Close the input file after encoding is complete
    outFile.close(); //-> Close the output file after writing is complete

    //-> Free the Huffman tree memory to prevent memory leaks
    delete root;

    //->[Compression Statistics Calculation]
    uint64_t compressedSize = fs::file_size(outputPath); //-> Get the size of the compressed output file
    double spaceSaved = 0.0; //-> Initialize space saved percentage
    if (originalSize > 0) {
        spaceSaved = (1.0 - (static_cast<double>(compressedSize) / static_cast<double>(originalSize))) * 100.0; //-> Calculate the percentage of space saved by compression
    }

    cout << COLOR_SKY_BLUE << "\n========================================================================\n\n" << COLOR_RESET;
    cout << COLOR_WHITE << "                      COMPRESSION SUMMARY REPORT                        \n\n";
    cout << COLOR_SKY_BLUE << "========================================================================\n" << COLOR_RESET;
    cout << COLOR_GREEN << "  Original File:          " << COLOR_WHITE << inputPath << "\n"; //-> Display the path of the original input file
    cout << COLOR_GREEN << "  Compressed File:        " << COLOR_WHITE << outputPath << "\n"; //-> Display the path of the compressed output file
    cout << COLOR_GREEN << "  Original File Size:     " << COLOR_WHITE << originalSize << " bytes\n"; //-> Display the size of the original input file in bytes
    cout << COLOR_GREEN << "  Compressed File Size:   " << COLOR_WHITE << compressedSize << " bytes\n"; //-> Display the size of the compressed output file in bytes
    cout << COLOR_GREEN << "  Space Saved:            " << COLOR_WHITE << fixed << setprecision(2) << spaceSaved << " %\n"; //-> Display the percentage of space saved by compression, formatted to two decimal places
    cout << COLOR_GREEN << "  Compression Ratio:      " << COLOR_WHITE << fixed << setprecision(2) //-> Display the compression ratio, calculated as original size divided by compressed size, formatted to two decimal places
              << (compressedSize > 0 ? (static_cast<double>(originalSize) / compressedSize) : 0.0) << " : 1\n"; //-> Handle division by zero if compressed size is zero
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
