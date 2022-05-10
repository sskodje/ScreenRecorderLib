#include <cassert>
#include <tuple>
#include <vector>
#include <Windows.h>
#include <stdexcept>

/** Process atoms within a MPEG4 MovieFragment (moof) to make the stream comply with ISO base media file format (https://www.iso.org/standard/68960.html).
    Work-around for shortcommings in the Media Foundation MPEG4 file sink (https://msdn.microsoft.com/en-us/library/windows/desktop/dd757763).
    Please delete this class if a better alternative becomes available.
Expected atom hiearchy:
[moof] movie fragment
* [mfhd] movie fragment header
* [traf] track fragment
  - [tfhd] track fragment header (will be modified)
  - [tfdt] track fragment decode timebox (will be added)
  - [trun] track run (will be modified) */
class MP4FragmentEditor {
    static constexpr unsigned long S_HEADER_SIZE = 8;

public:
    MP4FragmentEditor() {
    }

    /** Intended to be called from IMFByteStream::BeginWrite and IMFByteStream::Write before forwarding the data to a socket.
        Will modify the "moof" atom if present.
        returns a (ptr, size) tuple pointing to a potentially modified buffer. */
    std::tuple<const BYTE *, ULONG> EditStream(const BYTE *buf, ULONG size) {
        if (size < 5 * S_HEADER_SIZE)
            return std::tie(buf, size); // too small to contain a moof (skip processing)

        uint32_t atom_size = GetAtomSize(buf);
        assert(atom_size <= size);

        if (IsAtomType(buf, "moof")) {
            // Movie Fragment (moof)
            assert(atom_size == size);
            return ModifyMovieFragment(buf, atom_size);
        }

        return std::tie(buf, size);
    }

private:
    /** REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/MovieFragmentBox.java */
    std::tuple<const BYTE *, ULONG> ModifyMovieFragment(const BYTE *buf, const ULONG size) {
        assert(GetAtomSize(buf) == size);

        // copy to temporary buffer before modifying & extending atoms
        m_write_buf.resize(size - 8 + 20);
        memcpy(m_write_buf.data()/*dst*/, buf, size);

        BYTE *moof_ptr = m_write_buf.data(); // switch to internal buffer

        // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/MovieFragmentHeaderBox.java
        BYTE *mfhd_ptr = moof_ptr + S_HEADER_SIZE;
        uint32_t mfhd_size = GetAtomSize(mfhd_ptr);
        if (!IsAtomType(mfhd_ptr, "mfhd")) // movie fragment header
            throw std::runtime_error("not a \"mfhd\" atom");
        auto seq_nr = DeSerialize<uint32_t>(mfhd_ptr + S_HEADER_SIZE + 4); // increases by one per fragment
        seq_nr;

        // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/TrackFragmentBox.java
        BYTE *traf_ptr = mfhd_ptr + mfhd_size; // jump to next atom (don't inspect mfhd fields)
        uint32_t traf_size = GetAtomSize(traf_ptr);
        if (!IsAtomType(traf_ptr, "traf")) // track fragment
            throw std::runtime_error("not a \"traf\" atom");
        BYTE *tfhd_ptr = traf_ptr + S_HEADER_SIZE;

        unsigned long pos_idx = static_cast<unsigned long>(tfhd_ptr - moof_ptr);
        int rel_size = ProcessTrackFrameChildren(m_write_buf.data() + pos_idx, size, size - pos_idx);
        if (rel_size) {
            // size have changed - update size of parent atoms
            Serialize<uint32_t>(moof_ptr, size + rel_size);
            Serialize<uint32_t>(traf_ptr, traf_size + rel_size);
        }
        return std::make_tuple(moof_ptr, size + rel_size);
    }


    /** Modify the FrackFrame (traf) child atoms to comply with https://www.w3.org/TR/mse-byte-stream-format-isobmff/#movie-fragment-relative-addressing
    Changes done:
    * Modify TrackFragmentHeader (tfhd):
      - remove base-data-offset parameter (reduces size by 8bytes)
      - set default-base-is-moof flag
    * Add missing track fragment decode timebox (tfdt) (increases size by 20bytes)
    * Modify track run box (trun):
      - modify data_offset

    Returns the relative size of the modified child atoms (bytes shrunk or grown). */
    int ProcessTrackFrameChildren(BYTE *tfhd_ptr, ULONG moof_size, ULONG buf_size) {
        const unsigned long HEADER_SIZE = 8; // atom header size (4bytes size + 4byte name)
        const unsigned long FLAGS_SIZE = 4; // atom flags size (1byte version + 3bytes with flags)
        const unsigned int BASE_DATA_OFFSET_SIZE = 8; // size of tfhd flag to remove
        const unsigned int TFDT_SIZE = 20; // size of new tfdt atom that is added

        if (buf_size < 2 * HEADER_SIZE + 8)
            return 0; // too small to contain tfhd & trun atoms

        // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/TrackFragmentHeaderBox.java
        uint32_t tfhd_size = GetAtomSize(tfhd_ptr);
        {
            if (!IsAtomType(tfhd_ptr, "tfhd")) // track fragment header
                return 0; // not a "tfhd" atom

            // process tfhd content
            const BYTE DEFAULT_BASE_IS_MOOF_FLAG = 0x02; // stored at offset 1
            const BYTE BASE_DATA_OFFSET_FLAG = 0x01; // stored at offset 3
            BYTE *payload = tfhd_ptr + HEADER_SIZE;
            // 1: set default-base-is-moof flag
            payload[1] |= DEFAULT_BASE_IS_MOOF_FLAG;

            if (!(payload[3] & BASE_DATA_OFFSET_FLAG))
                return 0; // base-data-offset not set

            // 2: remove base-data-offset flag
            payload[3] &= ~BASE_DATA_OFFSET_FLAG;
            Serialize<uint32_t>(tfhd_ptr, tfhd_size - BASE_DATA_OFFSET_SIZE); // shrink atom size

            payload += FLAGS_SIZE; // skip "flags" field
            payload += 4;          // skip track-ID field (4bytes)

            // move remaining tfhd fields over data_offset
            size_t remaining_size = tfhd_size - HEADER_SIZE - FLAGS_SIZE - 4 - BASE_DATA_OFFSET_SIZE;
            MemMove(payload/*dst*/, payload + BASE_DATA_OFFSET_SIZE/*src*/, remaining_size/*size*/);
        }
        // pointer to right after shrunken tfhd atom
        BYTE *ptr = tfhd_ptr + tfhd_size - BASE_DATA_OFFSET_SIZE;

        // move "trun" atom to make room for a new "tfhd"
        MemMove(ptr + TFDT_SIZE - BASE_DATA_OFFSET_SIZE/*dst*/, ptr/*src*/, buf_size - tfhd_size/*size*/);

        {
            BYTE *tfdt_ptr = ptr;
            // insert an empty "tfdt" atom (20bytes)
            // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/TrackFragmentBaseMediaDecodeTimeBox.java
            Serialize<uint32_t>(tfdt_ptr, TFDT_SIZE);
            memcpy(tfdt_ptr + 4/*dst*/, "tfdt", 4); // track fragment base media decode timebox
            tfdt_ptr += HEADER_SIZE;
            *tfdt_ptr = 1; // version 1 (no other flags)
            tfdt_ptr += FLAGS_SIZE; // skip flags
            // write tfdt/baseMediaDecodeTime
            Serialize<uint64_t>(tfdt_ptr, m_cur_time);
            tfdt_ptr += sizeof(uint64_t);
        }

        {
            // REF: https://github.com/sannies/mp4parser/blob/master/isoparser/src/main/java/org/mp4parser/boxes/iso14496/part12/TrackRunBox.java
            BYTE *trun_ptr = ptr + TFDT_SIZE;
            uint32_t trun_size = GetAtomSize(trun_ptr);
            trun_size;
            if (!IsAtomType(trun_ptr, "trun")) // track run box
                throw std::runtime_error("not a \"trun\" atom");
            BYTE *payload = trun_ptr + HEADER_SIZE;
            assert(payload[0] == 1);   // check version
            const BYTE SAMPLE_DURATION_PRESENT_FLAG = 0x01; // stored at offset 2
            const BYTE DATA_OFFSET_PRESENT_FLAG = 0x01;     // stored at offset 3
            assert(payload[2] & SAMPLE_DURATION_PRESENT_FLAG); // verify that sampleDurationPresent is set
            assert(payload[3] & DATA_OFFSET_PRESENT_FLAG); // verify that dataOffsetPresent is set
            payload += FLAGS_SIZE; // skip flags

            auto sample_count = DeSerialize<uint32_t>(payload);
            assert(sample_count > 0);
            payload += sizeof(sample_count);

            // overwrite data_offset field
            Serialize<uint32_t>(payload, moof_size - BASE_DATA_OFFSET_SIZE + TFDT_SIZE + 8); // +8 experiementally derived
            payload += sizeof(uint32_t);

            auto sample_dur = DeSerialize<uint32_t>(payload); // duration of first sample

            // update baseMediaDecodeTime for next fragment
            m_cur_time += sample_count * sample_dur;
        }

        return TFDT_SIZE - BASE_DATA_OFFSET_SIZE; // tfdt added, tfhd shrunk
    }

    /** Deserialize & conververt from big-endian. */
    template <typename T>
    static T DeSerialize(const BYTE *buf) {
        T val = {};
        for (size_t i = 0; i < sizeof(T); ++i)
            reinterpret_cast<BYTE *>(&val)[i] = buf[sizeof(T) - 1 - i];

        return val;
    }

    /** Serialize & conververt to big-endian. */
    template <typename T>
    static void Serialize(BYTE *buf, T val) {
        for (size_t i = 0; i < sizeof(T); ++i)
            buf[i] = reinterpret_cast<BYTE *>(&val)[sizeof(T) - 1 - i];
    }

    /** Mofified version of "memmove" that clears the abandoned bytes, as well as intermediate data.
    WARNING: Only use for contiguous/overlapping moves, or else it will clear more than excpected. */
    static void MemMove(BYTE *dest, const BYTE *source, size_t num) {
        // move memory block
        memmove(dest, source, num);

        // clear abandoned byte range
        if (dest > source)
            memset(const_cast<BYTE *>(source)/*dst*/, 0/*val*/, dest - source/*size*/);
        else
            memset(dest + num/*dst*/, 0/*val*/, source - dest/*size*/);
    }

    /** Check if an MPEG4 atom is of a given type. */
    static bool IsAtomType(const BYTE *atom_ptr, const char type[4]) {
        return memcmp(atom_ptr + 4, type, 4) == 0; // atom type is stored at offset 4-7
    }

    /** Get the size of an MPEG4 atom. */
    static uint32_t GetAtomSize(const BYTE *atom_ptr) {
        return DeSerialize<uint32_t>(atom_ptr);
    }

private:
    uint64_t          m_cur_time = 0;
    std::vector<BYTE> m_write_buf; ///< write buffer (used when modifying moof atoms)
};
