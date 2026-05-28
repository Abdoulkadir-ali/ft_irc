#ifndef CHANNEL_HPP
# define CHANNEL_HPP

# include "Client.hpp"
# include <map>
# include <set>
# include <string>

class Channel {
public:
	explicit Channel(const std::string &name);
	~Channel();

	// --- Getters ---
	const std::string	&getName() const;
	const std::string	&getTopic() const;
	const std::string	&getKey() const;
	int					getUserLimit() const;
	bool				isInviteOnly() const;
	bool				isTopicRestricted() const;
	bool				hasKey() const;
	bool				hasUserLimit() const;
	std::string			getModeString() const;

	// --- Setters ---
	void	setTopic(const std::string &topic);
	void	setKey(const std::string &key);
	void	removeKey();
	void	setUserLimit(int limit);
	void	removeUserLimit();
	void	setInviteOnly(bool on);
	void	setTopicRestricted(bool on);

	// --- Members ---
	void	addMember(Client *client);
	void	removeMember(Client *client);
	bool	isMember(Client *client) const;
	int		memberCount() const;
	const std::map<int, Client *>	&getMembers() const;

	// --- Operators ---
	void	addOperator(Client *client);
	void	removeOperator(Client *client);
	bool	isOperator(Client *client) const;

	// --- Invite list ---
	void	addInvited(const std::string &nick);
	bool	isInvited(const std::string &nick) const;
	void	removeInvited(const std::string &nick);

	// --- Broadcast ---
	// Send message to all members except 'except' (can be NULL)
	void	broadcast(const std::string &message, Client *except);

private:
	Channel();
	Channel(const Channel &);
	Channel &operator=(const Channel &);

	std::string					_name;
	std::string					_topic;
	std::string					_key;
	int							_user_limit;
	bool						_invite_only;
	bool						_topic_restricted;
	std::map<int, Client *>		_members;		// fd -> Client*
	std::set<int>				_operators;		// fd set
	std::set<std::string>		_invited;		// lowercase nicks
};

#endif
