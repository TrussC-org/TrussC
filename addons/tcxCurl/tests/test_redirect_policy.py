"""Keep the C++ credential consumers and Java mirror on one policy."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[3]


class RedirectPolicyTest(unittest.TestCase):
    def test_header_value_validation_before_android_io(self):
        java = (ROOT / 'addons/tcxCurl/android/java/com/trussc/tcx/curl/AndroidHttpClient.java').read_text(encoding='utf-8')
        cpp = (ROOT / 'addons/tcxCurl/src/tcxCurl.h').read_text(encoding='utf-8')
        jni = (ROOT / 'addons/tcxCurl/src/tcxCurl_android.cpp').read_text(encoding='utf-8')
        for forbidden in (r"value.indexOf('\r')", r"value.indexOf('\n')", r"value.indexOf('\0')"):
            self.assertIn(forbidden, java)
        self.assertLess(java.index('for (String value : headerValues) validateHeaderValue(value)'),
                        java.index('target.openConnection()'))
        self.assertIn('!detail::validHeaderValue(h.second)', cpp)
        self.assertIn('reportInvalidRequestHeader(invalidHeaderWarned_, invalid)', cpp)
        request = jni.split('HttpResponse HttpClient::request', 1)[1]
        self.assertLess(request.index('prepareHeaders(hs, invalid,'), request.index('return callJavaRequest('))
        self.assertIn('body.empty() ? nullptr : &contentType', request)

    def test_raw_zone_policy_mirror(self):
        java = (ROOT / 'addons/tcxCurl/android/java/com/trussc/tcx/curl/AndroidHttpClient.java').read_text(encoding='utf-8')
        cpp = (ROOT / 'addons/tcxCurl/src/tcxCurl.h').read_text(encoding='utf-8')
        # Source parity only: Android/JVM execution is a separate integration check.
        zone = java.split('static String originZone', 1)[1].split('static boolean sameHost', 1)[0]
        self.assertIn('host.startsWith("%25", scope) ? 3 : 1', zone)
        self.assertIn('host.substring(start, end)', zone)
        self.assertNotIn('decode', zone.split('int start', 1)[1])
        host = java.split('static boolean sameHost', 1)[1].split('static final class LimitException', 1)[0]
        self.assertIn("leftZone.indexOf('%') < 0 && rightZone.indexOf('%') < 0", host)
        self.assertIn('leftZone.equals(rightZone)', host)
        self.assertIn('curl_url_get(parsed, CURLUPART_ZONEID, &scope, 0)', cpp)
        self.assertIn("zone.find('%') != std::string::npos", cpp)
        self.assertIn("nextZone.find('%') != std::string::npos", cpp)

    def test_shared_credential_list(self):
        cpp = (ROOT / 'addons/tcxCurl/src/tcxCurl.h').read_text(encoding='utf-8')
        java = (ROOT / 'addons/tcxCurl/android/java/com/trussc/tcx/curl/AndroidHttpClient.java').read_text(encoding='utf-8')
        expected = ['Authorization', 'Proxy-Authorization', 'X-Api-Key', 'Api-Key', 'X-Auth-Token', 'Cookie']
        native = re.search(r'credentialHeaders\[\]\s*=\s*\{(.*?)\}', cpp, re.S).group(1)
        mirrored = re.search(r'CREDENTIAL_HEADERS\s*=\s*\{(.*?)\}', java, re.S).group(1)
        self.assertEqual(re.findall(r'"([^"]+)"', native), expected)
        self.assertEqual(re.findall(r'"([^"]+)"', mirrored), expected)
        # Assert the consumers call the same classifier, rather than duplicating names.
        redaction = cpp.split('inline bool credentialNameAt', 1)[1].split('// curl echoes', 1)[0]
        redirect = cpp.split('void performHops', 1)[1].split('// setVerbose() output:', 1)[0]
        self.assertIn('isCredentialHeader(line.substr', redaction)
        self.assertIn('!forwardCredentials && detail::isCredentialHeader(h.first)', redirect)
        self.assertIn('!forwardCredentials && isCredentialHeader(name)', java)
        self.assertIn('for (String candidate : CREDENTIAL_HEADERS)', java)
        classifier = cpp.split('inline bool isCredentialHeader', 1)[1].split('inline bool credentialNameAt', 1)[0]
        self.assertIn('name = trimHeaderName(name)', classifier)
        # Canonicalization/validation must precede any transport entry; every
        # mirrored name then reaches the same case-insensitive classifier.
        self.assertLess(java.index('headerNames[i] = canonicalHeaderName'), java.index('target.openConnection()'))
        self.assertIn('String name = headerNames[i]', java)
        for code in ('asciiWhitespace(name.charAt(start))', 'asciiWhitespace(name.charAt(end - 1))',
                     'trimmed.isEmpty()', 'Invalid HTTP header name', "!#$%&'*+-.^_`|~"):
            self.assertIn(code, java)
        # Behavioral variants for every entry are in the native fixtures.
        unit = (ROOT / 'addons/tcxCurl/tests/src/main.cpp').read_text(encoding='utf-8')
        integration = (ROOT / 'addons/tcxCurl/tests-curl/src/main.cpp').read_text(encoding='utf-8')
        self.assertIn('for (auto name : credentialHeaders)', unit)
        self.assertIn('isCredentialHeader(spelling)', unit)
        self.assertIn('redactCredentialLine(spelling', unit)
        self.assertIn('variants.addHeader(spelling', integration)
        self.assertIn('same/different origin: ', integration)
        for variants in (unit, integration):
            self.assertIn('mixed + "\\t"', variants)
            self.assertIn('" " + mixed', variants)
            self.assertIn('std::toupper(mixed[i])', variants)



if __name__ == '__main__':
    unittest.main()
