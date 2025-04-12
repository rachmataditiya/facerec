// Ekstrak fitur wajah
HFFaceFeature feature;
HResult result = HFFaceFeatureExtract(session, imageStream, faceData.tokens[0], &feature);
if (result != HSUCCEED) {
    std::cerr << "Gagal mengekstrak fitur wajah" << std::endl;
    return;
}

// Cari wajah yang cocok di database
HFFaceFeatureIdentity mostSimilar;
float confidence;
result = HFFeatureHubFaceSearch(feature, &confidence, &mostSimilar);
if (result != HSUCCEED) {
    std::cerr << "Gagal mencari wajah di database" << std::endl;
    return;
}

// Tampilkan hasil
if (confidence > 0.6) { // Threshold bisa disesuaikan
    std::cout << "Wajah dikenali dengan confidence: " << confidence << std::endl;
    std::cout << "ID: " << mostSimilar.id << std::endl;
} else {
    std::cout << "Wajah tidak dikenali" << std::endl;
}

// Bersihkan resource
HFReleaseFaceFeature(&feature); 