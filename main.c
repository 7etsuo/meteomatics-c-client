#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <jansson.h>
#include <errno.h>

#define ERROR(msg)                                                             \
  do                                                                           \
  {                                                                            \
    fprintf (stderr, "Error: %s at %s:%d\n", msg, __FILE__, __LINE__);         \
  } while (0)

#define ERROR_EXIT(msg)                                                        \
  do                                                                           \
  {                                                                            \
    ERROR (msg);                                                               \
    exit (EXIT_FAILURE);                                                       \
  } while (0)

#define API_MAX_URL_LENGTH 512
#define API_MAX_RESPONSE_SIZE (10 * 1024 * 1024) // 10MB
#define API_INITIAL_BUFFER_SIZE 4096

typedef enum
{
  WEATHER_SUCCESS = 0,
  WEATHER_ERROR_INVALID_CONFIG = -1,
  WEATHER_ERROR_INVALID_MEMORY = -2,
  WEATHER_ERROR_URL_CONSTRUCTION = -3,
  WEATHER_ERROR_NETWORK = -4,
  WEATHER_ERROR_JSON = -5
} WEATHER_ERROR;

typedef struct
{
  char *data;
  size_t size;
  size_t capacity;
  size_t max_response_size;
} ResponseBuffer;

typedef struct
{
  const char *username;
  const char *password;
  const char *datetime;
  const char *parameters;
  const char *location;
  const char *format;
} WeatherConfig;

typedef const char *const IMMUTABLE_CHAR_PTR;

static IMMUTABLE_CHAR_PTR API_BASE_URL = "https://api.meteomatics.com";
static IMMUTABLE_CHAR_PTR DEFAULT_DATETIME = "2024-10-23T00:00:00Z";
// this can all be found on the API docs for the webpage it's for the request
// for example 2m:C gives us celcius and the 2m i think 2m above sea level ?
static IMMUTABLE_CHAR_PTR DEFAULT_PARAMETERS
  = "t_2m:C,precip_1h:mm,wind_speed_10m:ms";
// this is Sanfran (the long / lat)
static IMMUTABLE_CHAR_PTR DEFAULT_LOCATION = "37.7749,-122.4194";
static IMMUTABLE_CHAR_PTR DEFAULT_FORMAT = "json";

// clang-format off
static WEATHER_ERROR cleanup_response_buffer (ResponseBuffer *buffer);

static WEATHER_ERROR init_response_buffer (ResponseBuffer *buffer);
static WEATHER_ERROR validate_config (const WeatherConfig *config);
static WEATHER_ERROR construct_url (const WeatherConfig *config, char *url, size_t url_size);
// this is the callback for the opts that libcurl needs
static size_t write_callback (void *contents, size_t size, size_t nmemb, void *userp);
static WEATHER_ERROR perform_request (const char *url, const WeatherConfig *config, ResponseBuffer *response);
static WEATHER_ERROR process_json (const char *json_data, json_t **processed_root);
// clang-format on

int
main (int argc, char **argv)
{
  CURLcode curl_status = curl_global_init (CURL_GLOBAL_ALL);
  if (CURLE_OK != curl_status)
    ERROR_EXIT (curl_easy_strerror (curl_status));

  WEATHER_ERROR status = WEATHER_SUCCESS;
  ResponseBuffer response = {0};

  status = init_response_buffer (&response);
  if (WEATHER_SUCCESS != status)
  {
    ERROR ("Failed to initialize response buffer\n");
    goto cleanup;
  }

  // these should be in your env variables
  // we have to build the url from these parameters so we don't init it
  WeatherConfig config = {.username = getenv ("METEOMATICS_USERNAME"),
			  .password = getenv ("METEOMATICS_PASSWORD"),
			  .datetime = DEFAULT_DATETIME,
			  .parameters = DEFAULT_PARAMETERS,
			  .location = DEFAULT_LOCATION,
			  .format = DEFAULT_FORMAT};

  status = validate_config (&config);
  if (WEATHER_SUCCESS != status)
  {
    ERROR ("Invalid Configuration\n");
    goto cleanup;
  }

  char url[API_MAX_URL_LENGTH] = {0};
  status = construct_url (&config, url, sizeof (url));
  if (WEATHER_SUCCESS != status)
  {
    ERROR ("Failed to construct URL\n");
    goto cleanup;
  }

  status = perform_request (url, &config, &response);
  if (WEATHER_SUCCESS != status)
  {
    ERROR ("Failed to perform API request\n");
    goto cleanup;
  }

  json_t *processed_json = NULL;
  status = process_json (response.data, &processed_json);
  if (WEATHER_SUCCESS != status)
  {
    ERROR ("Failed to process JSON response\n");
    goto cleanup;
  }

  char *formatted_output = json_dumps (processed_json, JSON_INDENT (2));
  if (formatted_output)
  {
    printf ("%s\n", formatted_output);
    free (formatted_output);
  }
  else
  {
    ERROR ("Failed to format JSON output\n");
    status = WEATHER_ERROR_JSON;
  }

cleanup:
  if (processed_json)
    json_decref (processed_json);

  cleanup_response_buffer (&response);
  curl_global_cleanup ();

  return (status == WEATHER_SUCCESS) ? EXIT_SUCCESS : EXIT_FAILURE;
}

static WEATHER_ERROR
init_response_buffer (ResponseBuffer *buffer)
{
  if (!buffer)
    return WEATHER_ERROR_INVALID_CONFIG;

  buffer->data = malloc (API_INITIAL_BUFFER_SIZE);
  if (!buffer->data)
    return WEATHER_ERROR_INVALID_MEMORY;

  buffer->max_response_size = API_MAX_RESPONSE_SIZE;
  buffer->capacity = API_INITIAL_BUFFER_SIZE;
  buffer->data[0] = '\0';
  buffer->size = 0;

  return WEATHER_SUCCESS;
}

static WEATHER_ERROR
validate_config (const WeatherConfig *config)
{
  if (!config)
    return WEATHER_ERROR_INVALID_CONFIG;

  if (!config->username || !config->password || strlen (config->username) == 0
      || strlen (config->password) == 0)
  {
    ERROR ("Error: Missing credentials in environment variables\n");
    return WEATHER_ERROR_INVALID_CONFIG;
  }

  return WEATHER_SUCCESS;
}

static WEATHER_ERROR
construct_url (const WeatherConfig *config, char *url, size_t url_size)
{
  if (!config || !url || url_size == 0)
    return WEATHER_ERROR_INVALID_CONFIG;

  int nwritten
    = snprintf (url, url_size, "%s/%s/%s/%s/%s", API_BASE_URL, config->datetime,
		config->parameters, config->location, config->format);

  if (nwritten < 0 || (size_t) nwritten >= url_size)
    return WEATHER_ERROR_URL_CONSTRUCTION;

  return WEATHER_SUCCESS;
}

static size_t
write_callback (void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  ResponseBuffer *buffer = (ResponseBuffer *) userp;

  if (buffer->size + realsize > buffer->capacity)
  {
    size_t new_size = buffer->capacity * 2;

    if (new_size > buffer->max_response_size)
    {
      fprintf (stderr, "Response too large (exceeds %zu bytes)\n",
	       buffer->max_response_size);
      return 0; // this will tell libcurl there was an error
    }

    char *new_data = realloc (buffer->data, new_size);
    if (!new_data)
    {
      fprintf (stderr, "Failed to allocate memory (exceeds %zu bytes)\n",
	       buffer->max_response_size);
      return 0; // this will tell libcurl there was an error
    }

    buffer->data = new_data;
    buffer->capacity = new_size;
  }

  memcpy (buffer->data + buffer->size, contents, realsize);
  buffer->size += realsize;
  buffer->data[buffer->size] = '\0';

  return realsize;
}

static WEATHER_ERROR
perform_request (const char *url, const WeatherConfig *config,
		 ResponseBuffer *response)
{
  if (!url || !config || !response)
    return WEATHER_ERROR_INVALID_CONFIG;

  CURL *curl = curl_easy_init ();
  if (!curl)
    return WEATHER_ERROR_NETWORK;

  curl_easy_setopt (curl, CURLOPT_URL, url);
  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt (curl, CURLOPT_WRITEDATA, response);
  curl_easy_setopt (curl, CURLOPT_USERNAME, config->username);
  curl_easy_setopt (curl, CURLOPT_PASSWORD, config->password);
  curl_easy_setopt (curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt (curl, CURLOPT_SSL_VERIFYHOST, 2L);

  CURLcode res = curl_easy_perform (curl);
  curl_easy_cleanup (curl);

  if (CURLE_OK != res)
  {
    ERROR (curl_easy_strerror (res));
    return WEATHER_ERROR_NETWORK;
  }

  return WEATHER_SUCCESS;
}

static WEATHER_ERROR
process_json (const char *json_data, json_t **processed_root)
{
  if (!json_data || !processed_root)
    return WEATHER_ERROR_INVALID_CONFIG;

  json_error_t error;
  json_t *root = json_loads (json_data, 0, &error);
  if (!root)
  {
    fprintf (stderr, "JSON parsing error on line %d: %s\n", error.line,
	     error.text);
    return WEATHER_ERROR_JSON;
  }

  json_object_del (root, "user");     // dont want to leak my API key / Name
  json_object_del (root, "password"); // dont want to leak my API key / Name
  json_object_del (root,
		   "credentials"); // dont want to leak my API key / Name

  *processed_root = root;
  return WEATHER_SUCCESS;
}

static WEATHER_ERROR
cleanup_response_buffer (ResponseBuffer *buffer)
{
  if (buffer && buffer->data)
  {
    free (buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
    buffer->capacity = 0;
  }
  return WEATHER_SUCCESS;
}
