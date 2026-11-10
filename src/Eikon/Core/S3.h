#pragma once

// S3Lean: AWS Signature Version 4 for the upload PUT,
// the Test Connection DELETE, the clock-skew HEAD and presigned GET URLs, plus the settings checks,
// object key, content types and error texts around them. Hashing uses BCrypt; the HTTP side is in
// Platform/Uploader. Protocol strings are UTF-8 (std::string); user-visible text is std::wstring.
//
// The generic part (CanonicalRequest, StringToSign, Signature, PresignQuery) follows the SigV4
// specification and is checked against AWS's published test vectors; part fixes the
// header set (content-type;host;x-amz-content-sha256;x-amz-date), the service "s3" and the URL rules.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Eikon::Core::S3
{
    // ---- Hashing (BCrypt SHA-256 / HMAC-SHA256) ----
    std::vector<std::uint8_t> Sha256(std::span<std::uint8_t const> data);
    std::string Sha256Hex(std::span<std::uint8_t const> data);
    std::string Sha256Hex(std::string_view data);
    std::vector<std::uint8_t> HmacSha256(std::span<std::uint8_t const> key, std::string_view data);
    std::string ToHex(std::span<std::uint8_t const> bytes);   // lowercase

    std::string ToUtf8(std::wstring_view text);
    std::wstring FromUtf8(std::string_view text);

    // RFC 3986 percent-encoding: A–Z a–z 0–9 - . _ ~ are kept, every other byte of the UTF-8 text
    // becomes %XX (uppercase hex), "/" included.
    std::string UriEncode(std::string_view text);
    // Splits on "/" and encodes each segment, keeping the slashes: "/a b/c$" -> "/a%20b/c%24".
    std::string EncodePath(std::string_view path);

    // ---- SigV4 (generic) ----
    struct Header
    {
        std::string name;    // any case; lowercased in the canonical form
        std::string value;   // trimmed in the canonical form
    };

    struct CanonicalInput
    {
        std::string method;          // "PUT"
        std::string uri;             // already encoded ("/bucket/key")
        std::string query;           // canonical query string, already encoded and sorted ("" for none)
        std::vector<Header> headers; // the signed headers
        std::string payloadHash;     // hex SHA-256 of the body, or "UNSIGNED-PAYLOAD"
    };

    // "content-type;host;x-amz-content-sha256;x-amz-date": lowercase names, sorted, ";"-joined.
    std::string SignedHeaders(std::vector<Header> const& headers);
    // method \n uri \n query \n canonical headers ("name:value\n" each) \n signed headers \n payload hash
    std::string CanonicalRequest(CanonicalInput const& input);
    std::string CredentialScope(std::string_view date, std::string_view region, std::string_view service);
    // "AWS4-HMAC-SHA256\n<amzDate>\n<scope>\n<hex sha256(canonical request)>"
    std::string StringToSign(std::string_view amzDate, std::string_view scope, std::string_view canonicalRequest);
    // HMAC("AWS4"+secret, date) -> region -> service -> "aws4_request"
    std::vector<std::uint8_t> SigningKey(std::string_view secret, std::string_view date, std::string_view region, std::string_view service);
    std::string Signature(std::string_view secret, std::string_view date, std::string_view region, std::string_view service,
                          std::string_view stringToSign);

    // Query parameters of a presigned URL, in SigV4's sorted order, without X-Amz-Signature:
    // X-Amz-Algorithm, X-Amz-Credential, X-Amz-Date, X-Amz-Expires, X-Amz-SignedHeaders=host.
    std::string PresignQuery(std::string_view accessKey, std::string_view amzDate, std::string_view region,
                             std::string_view service, std::int64_t expires);

    // ---- Dates (UTC, en_US_POSIX) ----
    std::string AmzDate(std::int64_t unixSeconds);     // yyyyMMdd'T'HHmmss'Z'
    std::string ScopeDate(std::int64_t unixSeconds);   // yyyyMMdd
    // The HTTP Date header, "EEE',' dd MMM yyyy HH':'mm':'ss 'GMT'", as Unix seconds.
    std::optional<std::int64_t> ParseHttpDate(std::string_view text);

    // ---- S3 rules ----
    struct Settings
    {
        std::wstring accessKey;   // vault s3access
        std::wstring secretKey;   // vault s3secret
        std::wstring region;      // uploadS3Region
        std::wstring endpoint;    // uploadS3Endpoint
        std::wstring bucket;      // uploadS3Bucket
    };

    // Where requests go.
    struct Target
    {
        std::string host;      // signed Host value, "<bucket>.s3.<region>.amazonaws.com" or the endpoint
        std::string region;    // "auto" for an endpoint without a region
        std::string bucket;
        bool pathStyle = false;
    };

    // Applies: endpoint "https://" prefix removed (and trailing "/"), region "auto" for an
    // endpoint without one, Cloudflare R2's "/<bucket>" suffix removed. Returns the error text
    // "Region is required when no endpoint is provided." when neither is set.
    std::variant<Target, std::wstring> ResolveTarget(Settings const& settings);
    // "/<key>" (virtual-hosted) or "/<bucket>/<key>" (path style), each segment percent-encoded.
    std::string CanonicalUri(Target const& target, std::string_view key);

    // Content type from the key's extension, lowercase comparison.
    std::string ContentTypeForKey(std::string_view key);

    // "SCR-%Y%m%d-%T": %T = base-36 of round(secondOfDay / 2) + 1296.
    // `localTime` gives the calendar fields (year, month, day, secondOfDay) of the capture time.
    struct LocalTime
    {
        int year = 1970;
        int month = 1;
        int day = 1;
        int secondOfDay = 0;
    };
    std::string ObjectName(LocalTime const& time, std::string_view extension);
    // uploadS3Prefix trimmed of "/" at both ends; key = prefix + "/" + name, or name.
    std::string ObjectKey(std::wstring_view prefix, std::string_view name);

    // Signed PUT/DELETE/HEAD for header set. `contentType` is empty for DELETE/HEAD,
    // which then sign host;x-amz-content-sha256;x-amz-date.
    struct SignedRequest
    {
        std::string uri;                     // canonical URI, also the request path
        std::vector<Header> headers;         // Content-Type (if any), x-amz-content-sha256, x-amz-date, Authorization
        std::string authorization;
    };
    SignedRequest SignRequest(std::string_view method, Target const& target, std::string_view uri, std::string_view accessKey,
                              std::string_view secretKey, std::string_view contentType, std::string_view payloadHash,
                              std::int64_t unixSeconds);

    // Presigned GET: "https://<host><uri>?<query>&X-Amz-Signature=<hex>".
    std::string PresignedUrl(Target const& target, std::string_view key, std::string_view accessKey, std::string_view secretKey,
                             std::int64_t expires, std::int64_t unixSeconds);

    // "public" sharing. nullopt when the base URL lacks "." or "//"; a "/" is added if missing
    // and the key appended as is.
    std::optional<std::wstring> PublicUrl(std::wstring_view baseUrl, std::string_view key);

    // Whether a response should trigger the clock-skew HEAD and retry.
    bool ShouldRetryForClockSkew(int status, int attempt, std::string_view body);

    // Friendly text for known HTTP statuses and network error codes (negative),
    // otherwise "Upload failed with status: <code>" ("Delete failed with status: " for the test DELETE).
    std::wstring ErrorMessage(int code, bool forDelete = false);
    // The friendly text only (empty when the code is not in the table).
    std::wstring KnownErrorMessage(int code);

    // The order in which encodings are tried until one is ≤ kMaxUploadBytes.
    inline constexpr std::size_t kMaxUploadBytes = 6u * 1024u * 1024u;
    struct EncodingStep
    {
        bool jpeg = false;    // PNG otherwise
        bool halved = false;  // image halved first (only when dpr >= 2)
    };
    std::vector<EncodingStep> EncodingPlan(double dpr);
}
