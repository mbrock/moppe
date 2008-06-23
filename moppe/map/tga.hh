
#include <vector>

namespace moppe {
  namespace tga {
    enum type  { TYPE_GRAYSCALE = 3 };
    enum depth { DEPTH_8 = 8 };

    template <typename Stream>
    void 
    write (Stream&              stream,
	   const char*          data, 
	   unsigned short       width,
	   unsigned short       height,
	   type                 type,
	   depth                depth)
    {
      char header[18] = { 0 };

      header[2]  = type;

      header[12] = width;
      header[13] = width >> 8;

      header[14] = height;
      header[15] = height >> 8;

      header[16] = depth;
      header[17] = 1 << 5; // origin: upper left

      stream.write (header, sizeof header);
      stream.write (data, width * height * (depth / 8));
    }

    template <typename Stream, typename Array>
    void
    write_gray8 (Stream&        stream,
		 const Array&   data,
		 unsigned short width,
		 unsigned short height)
    {
      write (stream, &data[0], width, height, TYPE_GRAYSCALE, DEPTH_8);
    }
  }
}
