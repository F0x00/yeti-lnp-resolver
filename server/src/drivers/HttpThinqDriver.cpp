#include <sstream>
#include <cstring>

#include "log.h"
#include "drivers/modules/HttpClient.h"
#include "HttpThinqDriver.h"

/**************************************************************
 * Implementation helpers
***************************************************************/
/*
 * Callback function for processing HTTP response
 */
size_t static write_func(void * ptr, size_t size, size_t nmemb, void * userdata)
{
    if (userdata)
    {
      std::string & s = *(reinterpret_cast<std::string *>(userdata));
      s.append((char *)ptr, (size * nmemb));
    }

    return (size * nmemb);
}

/**************************************************************
 * Configuration implementation
***************************************************************/
/*
 * Driver configuration constructor
 *
 * @param[in] data    The raw configuration data
 * @param[in] drvId   The driver identifier
 * @param[in] drvName The string driver name
 */
CHttpThinqDriverCfg::CHttpThinqDriverCfg(const CDriverCfg::RawConfig_t & data,
                                         const ECDriverId drvId)
  : CDriverCfg(data)
{
  if (CDriverCfg::ECONFIG_DATA_AS_JSON_STRING == CDriverCfg::getFormatType())
  {
    const char * errMsg = nullptr;
    cJSON *      jData  = cJSON_Parse(data["parameters"].c_str());

    do
    {
      if (nullptr == jData)
      {
        errMsg = "driver JSON config parsing error";
        break;
      }

      mHost = getRawHost(jData);
      if (nullptr == mHost)
      {
        errMsg = "host value is invalid!";
        break;
      }

      mUserName = getRawUserName(jData);
      if ((nullptr == mUserName) || (0 == std::strlen(mUserName)))
      {
        errMsg = "user name value is invalid!";
        break;
      }

      mToken = getRawKey(jData, drvId);
      if (nullptr == mToken)
      {
        errMsg = "token value is invalid!";
        break;
      }

      mPort    = getRawPort(jData);
      mTimeout = getRawTimeout(jData);

    } while(0);

    if (nullptr != jData)
    {
      cJSON_Delete(jData);
    }
    if (nullptr != errMsg)
    {
      throw error(getLabel(), errMsg);
    }
  }
  else
  {
    mHost = getRawHost(data);
    if (nullptr == mHost)
    {
      throw error(getLabel(), "host value is invalid!");
    }

    mUserName = getRawUserName(data);
    if ((nullptr == mUserName) || (0 == std::strlen(mUserName)))
    {
      throw error(getLabel(), "user name value is invalid!");
    }

    mToken = getRawKey(data, drvId);
    if (nullptr == mToken)
    {
      throw error(getLabel(), "token value is invalid!");
    }

    mPort    = getRawPort(data);
    mTimeout = getRawTimeout(data);
  }
}

/**************************************************************
 * Driver implementation
***************************************************************/
/*
 * Driver constructor
 *
 * @param[in] data  The raw configuration data
 */
CHttpThinqDriver::CHttpThinqDriver(const CDriverCfg::RawConfig_t & data)
: CDriver(ECDriverId::ERESOLVER_DRIVER_HTTP_THINQ, "REST/ThinQ")
{
  mCfg.reset(new CHttpThinqDriverCfg(data, getId()));

  std::ostringstream url;
  CDriverCfg::CfgPort_t port = mCfg->getPort();

  url << "https://" << mCfg->getHost();

  if (port)
  {
    url << ":" << std::dec << port;
  }

  url << "/lrn/extended/";
  mURLPrefix = url.str();

  mURLSuffix = "?format=json";
}

/*
 * Show drive information
 *
 * @return none
 */
void CHttpThinqDriver::showInfo() const
{
  info("[%u/%s] '%s' driver => address <%s:%u> "
      "[username - %s, timeout - %u seconds]",
      mCfg->getUniqId(),
      mCfg->getLabel(), getName(),
      mCfg->getHost(), mCfg->getPort(),
      mCfg->getUserName(),
      mCfg->getTimeout());
}

/*
 * Executing resolving procedure
 *
 * @param[in] inData      The source data for making resolving
 * @param[out] outResult  The structure for saving resolving result
 * @return none
 *
 * @docs - https://api.thinq.com/docs/
 */
void CHttpThinqDriver::resolve(const string & inData, SResult_t & outResult) const
{
  /*
   * Request URL:
   * https://api.thinq.com/lrn/extended/9194841422?format=json
   */

  string dstURL = mURLPrefix + inData + mURLSuffix;
  dbg("resolving by URL: '%s'", dstURL.c_str());

  string replyBuf;
  try
  {
    CHttpClient http;

    http.setSSLVerification(false);
    http.setAuthType(CHttpClient::ECAuth::BASIC);
    http.setAuthData(mCfg->getUserName(),mCfg->geToken());
    http.setTimeout(mCfg->getTimeout());
    http.setWriteCallback(write_func, &replyBuf);
    http.setHeader("Content-Type: application/json");

    if (http.perform(dstURL) != CHttpClient::ECReqCode::OK)
    {
      dbg("error on perform request: %s", http.getErrorStr());
      throw error(http.getErrorStr());
    }
  }
  catch (CHttpClient::error & e)
  {
    throw error(e.what());
  }

  /*
   * Processing reply. Example:
   * {
   *    "lrn": "9198900000",
   *    "lerg": {
   *      "npa": "919",
   *      "nxx": "287",
   *      "y": "A",
   *      "lata": "426",
   *      "ocn": "7555",
   *      "company": "TW TELECOM OF NC",
   *      "rc": "DURHAM",
   *      "state": "NC"
   *    }
   * }
   */

  dbg("HTTP reply: %s", replyBuf.c_str());

  cJSON * j = cJSON_Parse(replyBuf.c_str());
  if (!j)
  {
    warn("couldn't parse reply as JSON format: '%s'", replyBuf.c_str());
    throw error("HTTP reply parsing error");
  }

  cJSON * jlrn = cJSON_GetObjectItem(j, "lrn");
  if (!jlrn)
  {
    dbg("no 'lrn' field in returned JSON object: %s", replyBuf.c_str());
    cJSON_Delete(j);
    throw error("unexpected response");
  }

  char * lrn = cJSON_Print(jlrn);
  outResult.localNumberPortability = lrn;
  free(lrn);

  outResult.rawData = replyBuf;

  // remove quotes from result LNP
  string & lnp = outResult.localNumberPortability;
  if (0 == lnp.compare(0, 1, "\""))
  {
    lnp.erase(lnp.begin());
  }
  if (0 == lnp.compare(lnp.size()-1, 1, "\""))
  {
    lnp.erase(lnp.end() - 1);
  }

  cJSON_Delete(j);
}


