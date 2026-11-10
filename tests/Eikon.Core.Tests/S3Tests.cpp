#include "pch.h"
#include "TestFramework.h"

#include "Core/S3.h"

#include <variant>

using namespace Eikon::Core;

namespace
{
    // AWS's SigV4 test suite (aws-c-auth/tests/aws-signing-test-suite/v4, also botocore's
    // aws4_testsuite): credentials, region, service and time shared by every case.
    constexpr std::string_view kSuiteAccess = "AKIDEXAMPLE";
    constexpr std::string_view kSuiteSecret = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";
    constexpr std::string_view kSuiteAmzDate = "20150830T123600Z";
    constexpr std::string_view kEmptyHash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

    // Amazon S3 developer guide examples (sig-v4-header-based-auth, sigv4-query-string-auth).
    std::string const kDocsAccess = std::string{ "AKIAIOSFODNN7" } + "EXAMPLE";
    constexpr std::string_view kDocsSecret = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";

    std::string SuiteSignature(S3::CanonicalInput const& input)
    {
        std::string const scope = S3::CredentialScope("20150830", "us-east-1", "service");
        return S3::Signature(kSuiteSecret, "20150830", "us-east-1", "service",
                             S3::StringToSign(kSuiteAmzDate, scope, S3::CanonicalRequest(input)));
    }

    S3::Target AwsTarget(std::string bucket, std::string region)
    {
        S3::Target target;
        target.bucket = bucket;
        target.region = region;
        target.host = bucket + ".s3." + region + ".amazonaws.com";
        return target;
    }
}

TEST(S3_HashesMatchKnownDigests)
{
    CHECK_EQ(S3::Sha256Hex(std::string_view{}), std::string{ kEmptyHash });
    // The PUT example's body and the suite's form body.
    CHECK_EQ(S3::Sha256Hex(std::string_view{ "Welcome to Amazon S3." }),
             std::string{ "44ce7dd67c959e0d3524ffac1771dfbba87d2b6b4b4e99e42034a8b803f8b072" });
    CHECK_EQ(S3::Sha256Hex(std::string_view{ "Param1=value1" }),
             std::string{ "9095672bbd1f56dfc5b65f3e153adc8731a4a654192329106275f4c7b24d0b6e" });
}

TEST(S3_SuiteGetVanilla)
{
    S3::CanonicalInput input{ "GET", "/", "", { { "Host", "example.amazonaws.com" }, { "X-Amz-Date", std::string{ kSuiteAmzDate } } },
                              std::string{ kEmptyHash } };
    CHECK_EQ(S3::CanonicalRequest(input), std::string{ "GET\n/\n\nhost:example.amazonaws.com\nx-amz-date:20150830T123600Z\n\n"
                                                       "host;x-amz-date\n" } + std::string{ kEmptyHash });
    CHECK_EQ(S3::StringToSign(kSuiteAmzDate, "20150830/us-east-1/service/aws4_request", S3::CanonicalRequest(input)),
             std::string{ "AWS4-HMAC-SHA256\n20150830T123600Z\n20150830/us-east-1/service/aws4_request\n"
                          "bb579772317eb040ac9ed261061d46c1f17a8133879d6129b6e1c25292927e63" });
    CHECK_EQ(SuiteSignature(input), std::string{ "5fa00fa31553b73ebf1942676e86291e8372ff2a2260956d9b8aae1d763fbf31" });
}

TEST(S3_SuitePostFormSignsBodyHashAndSortsHeaders)
{
    // Headers given out of order and in mixed case; the canonical form sorts and lowercases them.
    S3::CanonicalInput input{ "POST", "/", "",
                              { { "X-Amz-Date", std::string{ kSuiteAmzDate } },
                                { "Content-Type", "application/x-www-form-urlencoded" },
                                { "x-amz-content-sha256", S3::Sha256Hex(std::string_view{ "Param1=value1" }) },
                                { "Host", "example.amazonaws.com" },
                                { "Content-Length", "13" } },
                              S3::Sha256Hex(std::string_view{ "Param1=value1" }) };
    CHECK_EQ(S3::SignedHeaders(input.headers), std::string{ "content-length;content-type;host;x-amz-content-sha256;x-amz-date" });
    CHECK_EQ(SuiteSignature(input), std::string{ "d3875051da38690788ef43de4db0d8f280229d82040bfac253562e56c3f20e0b" });
}

TEST(S3_SuitePathEncoding)
{
    CHECK_EQ(S3::EncodePath("/-._~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"),
             std::string{ "/-._~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz" });
    CHECK_EQ(S3::EncodePath("/\xE1\x88\xB4"), std::string{ "/%E1%88%B4" });

    S3::CanonicalInput unreserved{ "GET", S3::EncodePath("/-._~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"), "",
                                   { { "Host", "example.amazonaws.com" }, { "X-Amz-Date", std::string{ kSuiteAmzDate } } },
                                   std::string{ kEmptyHash } };
    CHECK_EQ(SuiteSignature(unreserved), std::string{ "07ef7494c76fa4850883e2b006601f940f8a34d404d0cfa977f52a65bbf5f24f" });

    S3::CanonicalInput utf8{ "GET", S3::EncodePath("/\xE1\x88\xB4"), "",
                             { { "Host", "example.amazonaws.com" }, { "X-Amz-Date", std::string{ kSuiteAmzDate } } },
                             std::string{ kEmptyHash } };
    CHECK_EQ(SuiteSignature(utf8), std::string{ "8318018e0b0f223aa2bbf98705b62bb787dc9c0e678f255a891fd03141be5d85" });
}

TEST(S3_SuiteQueryOrderAndPresignQuery)
{
    S3::CanonicalInput ordered{ "GET", "/", "Param1=value1&Param2=value2",
                                { { "Host", "example.amazonaws.com" }, { "X-Amz-Date", std::string{ kSuiteAmzDate } } },
                                std::string{ kEmptyHash } };
    CHECK_EQ(SuiteSignature(ordered), std::string{ "b97d918cfa904a5beff61c982a1b6f458b799221646efd99d3219ec94cdf2500" });

    // get-vanilla, query-signed form.
    std::string const query = S3::PresignQuery(kSuiteAccess, kSuiteAmzDate, "us-east-1", "service", 3600);
    CHECK_EQ(query, std::string{ "X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=AKIDEXAMPLE%2F20150830%2Fus-east-1%2Fservice%2F"
                                 "aws4_request&X-Amz-Date=20150830T123600Z&X-Amz-Expires=3600&X-Amz-SignedHeaders=host" });
    S3::CanonicalInput presigned{ "GET", "/", query, { { "host", "example.amazonaws.com" } }, std::string{ kEmptyHash } };
    CHECK_EQ(SuiteSignature(presigned), std::string{ "e93c787ed7f371d5c6b165c1b38ede9550f4dce4144713e844b25b7192d3865d" });
}

TEST(S3_DocsGetAndPutObject)
{
    std::string const scope = S3::CredentialScope("20130524", "us-east-1", "s3");
    S3::CanonicalInput get{ "GET", "/test.txt", "",
                            { { "Host", "examplebucket.s3.amazonaws.com" }, { "Range", "bytes=0-9" },
                              { "x-amz-content-sha256", std::string{ kEmptyHash } }, { "x-amz-date", "20130524T000000Z" } },
                            std::string{ kEmptyHash } };
    CHECK_EQ(S3::StringToSign("20130524T000000Z", scope, S3::CanonicalRequest(get)),
             std::string{ "AWS4-HMAC-SHA256\n20130524T000000Z\n20130524/us-east-1/s3/aws4_request\n"
                          "7344ae5b7ee6c3e7e6b0fe0640412a37625d1fbfff95c48bbb2dc43964946972" });
    CHECK_EQ(S3::Signature(kDocsSecret, "20130524", "us-east-1", "s3", S3::StringToSign("20130524T000000Z", scope, S3::CanonicalRequest(get))),
             std::string{ "f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41" });

    std::string const body = S3::Sha256Hex(std::string_view{ "Welcome to Amazon S3." });
    S3::CanonicalInput put{ "PUT", S3::EncodePath("/test$file.text"), "",
                            { { "Date", "Fri, 24 May 2013 00:00:00 GMT" }, { "Host", "examplebucket.s3.amazonaws.com" },
                              { "x-amz-content-sha256", body }, { "x-amz-date", "20130524T000000Z" },
                              { "x-amz-storage-class", "REDUCED_REDUNDANCY" } },
                            body };
    CHECK_EQ(put.uri, std::string{ "/test%24file.text" });
    CHECK_EQ(S3::Signature(kDocsSecret, "20130524", "us-east-1", "s3", S3::StringToSign("20130524T000000Z", scope, S3::CanonicalRequest(put))),
             std::string{ "98ad721746da40c64f1a55b78f14c238d841ea1380cd77a1b5971af0ece108bd" });
}

TEST(S3_DocsPresignedUrl)
{
    S3::Target target;
    target.host = "examplebucket.s3.amazonaws.com";
    target.region = "us-east-1";
    target.bucket = "examplebucket";
    CHECK_EQ(S3::PresignedUrl(target, "test.txt", kDocsAccess, kDocsSecret, 86400, 1369353600),
             "https://examplebucket.s3.amazonaws.com/test.txt?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=" + kDocsAccess +
                 "%2F20130524%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20130524T000000Z&X-Amz-Expires=86400&X-Amz-SignedHeaders=host"
                 "&X-Amz-Signature=aeeed9bbccd4d02ee5c0109b86d86835f995330da4c265957d157751f604d404");
}

TEST(S3_PutHeadersAndAuthorization)
{
    // Expected values computed independently (Python hashlib/hmac) for header set.
    S3::Target const target = AwsTarget("my-bucket", "eu-central-1");
    std::string const key = S3::ObjectKey(L"/shots/", "SCR-20261108-a b.txt");
    std::string const uri = S3::CanonicalUri(target, key);
    CHECK_EQ(uri, std::string{ "/shots/SCR-20261108-a%20b.txt" });
    std::string const hash = S3::Sha256Hex(std::string_view{ "hello world" });
    auto const request = S3::SignRequest("PUT", target, uri, kSuiteAccess, kSuiteSecret, S3::ContentTypeForKey(key), hash, 1794145507);
    CHECK_EQ(request.authorization,
             std::string{ "AWS4-HMAC-SHA256 Credential=AKIDEXAMPLE/20261108/eu-central-1/s3/aws4_request, "
                          "SignedHeaders=content-type;host;x-amz-content-sha256;x-amz-date, "
                          "Signature=a0bcd17df5b43ed9361f7cb5e449c4227b5feae72aaa4ddbecbe9a2338371118" });
    CHECK_EQ(request.headers.size(), std::size_t{ 4 });
    CHECK_EQ(request.headers[0].value, std::string{ "text/plain" });
    CHECK_EQ(request.headers[1].value, hash);
    CHECK_EQ(request.headers[2].value, std::string{ "20261108T134507Z" });
}

TEST(S3_PresignPathStyleAutoRegion)
{
    S3::Settings settings{ L"AKIDEXAMPLE", L"secret", L"", L"https://abc123.r2.cloudflarestorage.com/shots-bucket", L"shots-bucket" };
    auto const resolved = S3::ResolveTarget(settings);
    CHECK(std::holds_alternative<S3::Target>(resolved));
    S3::Target const target = std::get<S3::Target>(resolved);
    CHECK_EQ(target.host, std::string{ "abc123.r2.cloudflarestorage.com" });
    CHECK_EQ(target.region, std::string{ "auto" });
    CHECK(target.pathStyle);
    CHECK_EQ(S3::PresignedUrl(target, "SCR-x.png", kSuiteAccess, kSuiteSecret, 604800, 1794145507),
             std::string{ "https://abc123.r2.cloudflarestorage.com/shots-bucket/SCR-x.png?X-Amz-Algorithm=AWS4-HMAC-SHA256"
                          "&X-Amz-Credential=AKIDEXAMPLE%2F20261108%2Fauto%2Fs3%2Faws4_request&X-Amz-Date=20261108T134507Z"
                          "&X-Amz-Expires=604800&X-Amz-SignedHeaders=host"
                          "&X-Amz-Signature=2427ab00da2b57940a32094a1a306bb552a34145e61fed4795a4cc9915c887a4" });
}

TEST(S3_ResolveTargetRules)
{
    // AWS: virtual-hosted style.
    auto aws = S3::ResolveTarget({ L"a", L"s", L"us-west-2", L"", L"pics" });
    CHECK_EQ(std::get<S3::Target>(aws).host, std::string{ "pics.s3.us-west-2.amazonaws.com" });
    CHECK(!std::get<S3::Target>(aws).pathStyle);
    CHECK_EQ(S3::CanonicalUri(std::get<S3::Target>(aws), "a/b c.png"), std::string{ "/a/b%20c.png" });

    // Endpoint with a region keeps it; path style puts the bucket first.
    auto minio = S3::ResolveTarget({ L"a", L"s", L"eu-1", L"minio.local:9000/", L"pics" });
    CHECK_EQ(std::get<S3::Target>(minio).host, std::string{ "minio.local:9000" });
    CHECK_EQ(std::get<S3::Target>(minio).region, std::string{ "eu-1" });
    CHECK_EQ(S3::CanonicalUri(std::get<S3::Target>(minio), "k.png"), std::string{ "/pics/k.png" });

    // Only cloudflarestorage.com endpoints lose a trailing "/<bucket>".
    auto other = S3::ResolveTarget({ L"a", L"s", L"", L"https://s3.example.com/pics", L"pics" });
    CHECK_EQ(std::get<S3::Target>(other).host, std::string{ "s3.example.com/pics" });
    auto r2 = S3::ResolveTarget({ L"a", L"s", L"", L"x.r2.cloudflarestorage.com/other", L"pics" });
    CHECK_EQ(std::get<S3::Target>(r2).host, std::string{ "x.r2.cloudflarestorage.com/other" });

    // "http://" is not stripped.
    auto http = S3::ResolveTarget({ L"a", L"s", L"", L"http://h.example", L"b" });
    CHECK_EQ(std::get<S3::Target>(http).host, std::string{ "http://h.example" });

    auto missing = S3::ResolveTarget({ L"a", L"s", L"", L"", L"b" });
    CHECK(std::holds_alternative<std::wstring>(missing));
    CHECK_EQ(std::get<std::wstring>(missing), std::wstring{ L"Region is required when no endpoint is provided." });
}

TEST(S3_ContentTypes)
{
    CHECK_EQ(S3::ContentTypeForKey("a/SCR-1.png"), std::string{ "image/png" });
    CHECK_EQ(S3::ContentTypeForKey("SCR-1.PNG"), std::string{ "image/png" });
    CHECK_EQ(S3::ContentTypeForKey("p.jpeg"), std::string{ "image/jpeg" });
    CHECK_EQ(S3::ContentTypeForKey("p.jpg"), std::string{ "image/jpeg" });
    CHECK_EQ(S3::ContentTypeForKey("x.svg"), std::string{ "image/svg+xml" });
    CHECK_EQ(S3::ContentTypeForKey("test_1234abcd.txt"), std::string{ "text/plain" });
    CHECK_EQ(S3::ContentTypeForKey("dir.png/file"), std::string{ "application/octet-stream" });
    CHECK_EQ(S3::ContentTypeForKey("noext"), std::string{ "application/octet-stream" });
}

TEST(S3_ObjectNameAndKey)
{
    // %T = base36(round(secondOfDay / 2) + 1296).
    CHECK_EQ(S3::ObjectName({ 2026, 11, 8, 0 }, "png"), std::string{ "SCR-20261108-100.png" });
    // 86399 / 2 = 43199.5 rounds away from zero to 43200; + 1296 = 44496 = "yc0".
    CHECK_EQ(S3::ObjectName({ 2026, 1, 2, 86399 }, "jpeg"), std::string{ "SCR-20260102-yc0.jpeg" });
    // 3 / 2 = 1.5 -> 2; 1298 = "102".
    CHECK_EQ(S3::ObjectName({ 2026, 11, 8, 3 }, "png"), std::string{ "SCR-20261108-102.png" });

    CHECK_EQ(S3::ObjectKey(L"/shots/", "n.png"), std::string{ "shots/n.png" });
    CHECK_EQ(S3::ObjectKey(L"a/b", "n.png"), std::string{ "a/b/n.png" });
    CHECK_EQ(S3::ObjectKey(L"///", "n.png"), std::string{ "n.png" });
    CHECK_EQ(S3::ObjectKey(L"", "n.png"), std::string{ "n.png" });
}

TEST(S3_PublicUrl)
{
    CHECK_EQ(S3::PublicUrl(L"https://cdn.example.com", "shots/n.png").value_or(L"?"), std::wstring{ L"https://cdn.example.com/shots/n.png" });
    CHECK_EQ(S3::PublicUrl(L"https://cdn.example.com/", "n b.png").value_or(L"?"), std::wstring{ L"https://cdn.example.com/n b.png" });
    CHECK(!S3::PublicUrl(L"cdn.example.com", "n.png"));
    CHECK(!S3::PublicUrl(L"https://localhost", "n.png"));
}

TEST(S3_ClockSkewRetryAndDates)
{
    CHECK(S3::ShouldRetryForClockSkew(403, 0, "<Code>RequestTimeTooSkewed</Code>"));
    CHECK(S3::ShouldRetryForClockSkew(403, 0, "<Code>SignatureDoesNotMatch</Code>"));
    CHECK(!S3::ShouldRetryForClockSkew(403, 1, "RequestTimeTooSkewed"));
    CHECK(!S3::ShouldRetryForClockSkew(400, 0, "RequestTimeTooSkewed"));
    CHECK(!S3::ShouldRetryForClockSkew(403, 0, "<Code>AccessDenied</Code>"));
    CHECK(!S3::ShouldRetryForClockSkew(403, 0, "time")); // case-sensitive, like String.contains

    CHECK_EQ(S3::ParseHttpDate("Fri, 24 May 2013 00:00:00 GMT").value_or(-1), std::int64_t{ 1369353600 });
    CHECK_EQ(S3::ParseHttpDate("Sun, 08 Nov 2026 13:45:07 GMT").value_or(-1), std::int64_t{ 1794145507 });
    CHECK(!S3::ParseHttpDate("Sun, 8 Nov 2026 13:45:07 GMT"));
    CHECK(!S3::ParseHttpDate("Sun, 08 Nov 2026 13:45:07 UTC"));
    CHECK(!S3::ParseHttpDate("Xyz, 08 Nov 2026 13:45:07 GMT"));
    CHECK_EQ(S3::AmzDate(1369353600), std::string{ "20130524T000000Z" });
    CHECK_EQ(S3::AmzDate(951782399), std::string{ "20000228T235959Z" });
    CHECK_EQ(S3::AmzDate(951782400), std::string{ "20000229T000000Z" }); // leap day
    CHECK_EQ(S3::ScopeDate(1794145507), std::string{ "20261108" });
}

TEST(S3_ErrorMessagesVerbatim)
{
    CHECK_EQ(S3::ErrorMessage(403), std::wstring{ L"Access Denied (403). Are Secret Key and Bucket correct? Try again." });
    CHECK_EQ(S3::ErrorMessage(412), std::wstring{ L"Precondition Failed (412). ETag/If-* headers didn\u2019t match." });
    CHECK_EQ(S3::ErrorMessage(429), std::wstring{ L"Too Many Requests (429). You\u2019re being throttled; back off and retry." });
    CHECK_EQ(S3::ErrorMessage(307), std::wstring{ L"Error 307. Bucket is in a different Region; update Region/Endpoint." });
    CHECK_EQ(S3::ErrorMessage(-1003), std::wstring{ L"Cannot find host (-1003). Endpoint or Region likely wrong." });
    CHECK_EQ(S3::ErrorMessage(-1202), std::wstring{ L"Secure connection: certificate not trusted (-1202). System clock may be wrong." });
    CHECK_EQ(S3::ErrorMessage(418), std::wstring{ L"Upload failed with status: 418" });
    CHECK_EQ(S3::ErrorMessage(418, true), std::wstring{ L"Delete failed with status: 418" });
    CHECK(S3::KnownErrorMessage(402).empty());
}

TEST(S3_EncodingPlan)
{
    auto const one = S3::EncodingPlan(1.0);
    CHECK_EQ(one.size(), std::size_t{ 2 });
    CHECK(!one[0].jpeg && !one[0].halved);
    CHECK(one[1].jpeg && !one[1].halved);
    CHECK_EQ(S3::EncodingPlan(1.5).size(), std::size_t{ 2 }); // halving needs dpr >= 2
    auto const two = S3::EncodingPlan(2.0);
    CHECK_EQ(two.size(), std::size_t{ 4 });
    CHECK(!two[2].jpeg && two[2].halved);
    CHECK(two[3].jpeg && two[3].halved);
    CHECK_EQ(S3::kMaxUploadBytes, std::size_t{ 6291456 });
}
