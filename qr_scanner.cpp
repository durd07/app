#include <stdio.h>
#include <stdlib.h>
#include <zbar.h>
#include <opencv2/opencv.hpp>

#include "daemon.h"

static char wift_str[512] = {0};

int qr_scan_init()
{
}

char * qr_scan(const char * img, int length)
{
    memset(wift_str, 0, 512);

    std::vector<char> img_vector(img, img + length);
    cv::Mat gray = cv::imdecode(img_vector, cv::IMREAD_GRAYSCALE);

    //cv::Mat gray = cv::imread(img, cv::IMREAD_GRAYSCALE);

    // Create a zbar scanner
    zbar::ImageScanner scanner;
    scanner.set_config(zbar::ZBAR_NONE, zbar::ZBAR_CFG_ENABLE, 1);

    // Create ZBar image from grayscale frame
    int width = gray.cols;
    int height = gray.rows;
    uchar *raw = (uchar *)gray.data;
    zbar::Image image(width, height, "Y800", raw, width * height);

    // Scan for barcodes
    scanner.scan(image);

    // Print barcode data if found
    for (zbar::Image::SymbolIterator symbol = image.symbol_begin();
         symbol != image.symbol_end(); ++symbol) {
        printf("Found %s barcode: %s\n",
               symbol->get_type_name().c_str(),
               symbol->get_data().c_str());
        memcpy(wift_str, symbol->get_data().c_str(), symbol->get_data().size());
	return wift_str;
        break;
    }

    return NULL;
}

int qr_scan_uninit()
{
}
