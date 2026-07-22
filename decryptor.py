from cryptography.fernet import Fernet

# Generate a key (share this key securely between sender and receiver)
# key = Fernet.generate_key()
# print("Generated Key:", key)
key = b'64Gn4JhjI5Ca-pOIix5Nztaq90zZsEFBLjqYVvhgBNo='
cipher = Fernet(key)

# Encrypt
message = "hello".encode()
encrypted_data = cipher.encrypt(message)

print("encrypted_data:", encrypted_data)
# Decrypt
decrypted_data = cipher.decrypt(b'gAAAAABqW0HerN6AWnJ9IHsZC1bWLb-lfUnAzDqq2mkLXdtJhVuyegdQVHGutAPhTMYaNXNoJTWvuGdONljQJfVQ9tJyDalhYg==')
print("decrypted_data:", decrypted_data.decode())